// The service's game files (#52): journal + replay per game, the newest unfinished game resumed, a cut-off last line
// dropped, a journal that doesn't fit set aside.
#include <gtest/gtest.h>

#include <sstream>

#include <random>

#include "game_store.hpp"
#include "session_game.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

game::HostConfig host_config() {
    game::HostConfig cfg;
    for (const auto& [key, file] : {std::pair{"vi", "vi-unleashed"}, std::pair{"zed", "zed-vendetta"}}) {
        const std::string text = deck_text(file);
        cfg.decks[key] = {parse_deck(db(), text), text};
    }
    return cfg;
}

struct TempDir {
    fs::path path = fs::temp_directory_path() / ("buddy-game-store-" + std::to_string(std::random_device{}()));
    ~TempDir() { fs::remove_all(path); }
};

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

// A bot game through the host as the service runs it, writing its files; stops after `inputs` inputs.
struct StoredGame {
    explicit StoredGame(game::GameStore& store) : store(store) {}
    game::GameStore& store;
    table::Sim sim{{}, {}, 3};
    table::Table table{sim, {}};
    game::GameHost host{db(), host_config()};
    std::vector<table::Command> pending;
    std::optional<game::AiRequest> ai;

    void absorb() {
        auto o = host.take();
        if (o.new_game) store.begin("2026-11-16-120000", host.session()->config().seed);
        store.append(o.journal, o.replay, o.replay_text);
        pending.insert(pending.end(), o.table.begin(), o.table.end());
        if (o.ai) ai = std::move(o.ai);
    }

    void play(int inputs) {
        for (int n = 0; n < inputs; ++n) {
            if (!pending.empty()) {
                const auto c = pending.front();
                pending.erase(pending.begin());
                if (c.kind == CommandKind::Setup) sim.configure({c.sim_deck, c.sim_runes, c.legend, c.champion, true});
                for (const auto& e : table.execute(c)) {
                    host.on_table(e);
                    absorb();
                }
            } else if (ai) {
                const auto r = std::move(*ai);
                ai.reset();
                ai::GreedyPlayer p(r.id);
                const auto& cfg = host.session()->config();
                p.begin(cfg.buddy, cfg.decks[0], cfg.decks[1]);
                host.on_ai(r.id, p.choose(r.view, r.actions));
                absorb();
            } else {
                return;  // over
            }
        }
    }
};

} // namespace

TEST(RiftboundGameStore, NoGameNothingToResume) {
    TempDir d;
    game::GameStore store(d.path);
    EXPECT_TRUE(fs::is_directory(d.path));
    EXPECT_FALSE(store.latest());
    store.append({"x"}, {"y"});  // no game: dropped
    EXPECT_TRUE(fs::is_empty(d.path));
    const auto r = game::resume_latest(store, db(), host_config());
    EXPECT_EQ(r.host, nullptr);
    EXPECT_EQ(r.note, "");
}

TEST(RiftboundGameStore, AnUndoRewritesTheReplay) {  // #97
    TempDir d;
    game::GameStore store(d.path);
    store.begin("2026-11-27-120000", 7);
    store.append({"j1"}, {"a", "b", "c"});
    store.append({"j2"}, {"d"}, std::string("a\nb\n"));  // the undo: the whole replay, then the new lines
    store.append({"j3"}, {"e"});
    const auto read = [](const fs::path& f) {
        std::ifstream in(f);
        std::stringstream t;
        t << in.rdbuf();
        return t.str();
    };
    EXPECT_EQ(read(d.path / "2026-11-27-120000-7.txt"), "a\nb\nd\ne\n");
    EXPECT_EQ(read(d.path / "2026-11-27-120000-7.journal"), "j1\nj2\nj3\n");  // the journal only grows
}

TEST(RiftboundGameStore, AnUnfinishedGameResumes) {
    TempDir d;
    game::GameStore store(d.path);
    StoredGame g(store);
    ASSERT_TRUE(g.host.command({{"action", "new"}, {"buddy", "vi"}, {"dev", "greedy"}, {"seed", 21}}).ok);
    g.absorb();
    g.play(50);
    ASSERT_NE(g.host.session()->status(), Status::Over);
    EXPECT_TRUE(fs::exists(d.path / "2026-11-16-120000-21.journal"));
    EXPECT_EQ(read(d.path / "2026-11-16-120000-21.txt"), g.host.session()->replay_text());

    // the service dies mid-write: the replay's last line cut off (its journal line is complete), or the next
    // journal line cut off (its input was never acted on)
    const fs::path txt = d.path / "2026-11-16-120000-21.txt";
    fs::resize_file(txt, fs::file_size(txt) - 3);
    std::ofstream(d.path / "2026-11-16-120000-21.journal", std::ios::app) << R"({"table":{"ev)";

    game::GameStore again(d.path);
    auto r = game::resume_latest(again, db(), host_config());
    ASSERT_NE(r.host, nullptr) << r.note;
    EXPECT_NE(r.note.find("resumed"), std::string::npos);
    EXPECT_EQ(r.host->state(), g.host.state());
    EXPECT_EQ(read(d.path / "2026-11-16-120000-21.txt"), g.host.session()->replay_text());  // rewritten
    EXPECT_EQ(read(d.path / "2026-11-16-120000-21.journal").back(), '\n');                 // cut-off line gone
    EXPECT_EQ(again.journal_path(), d.path / "2026-11-16-120000-21.journal");
    again.append({R"({"table_lost":"timeout"})"}, {});
    EXPECT_EQ(again.latest()->lines.back(), R"({"table_lost":"timeout"})");
}

TEST(RiftboundGameStore, AFinishedGameIsNotResumed) {
    TempDir d;
    game::GameStore store(d.path);
    StoredGame g(store);
    ASSERT_TRUE(g.host.command({{"action", "new"}, {"buddy", "zed"}, {"dev", "greedy"}, {"seed", 22}}).ok);
    g.absorb();
    g.play(1'000'000);
    ASSERT_EQ(g.host.session()->status(), Status::Over);
    const auto r = game::resume_latest(store, db(), host_config());
    EXPECT_EQ(r.host, nullptr);
    EXPECT_NE(r.note.find("over"), std::string::npos);
}

TEST(RiftboundGameStore, TheNewestGameCountsAndNamesDontClash) {
    TempDir d;
    game::GameStore store(d.path);
    store.begin("2026-11-16-120000", 5);
    store.begin("2026-11-16-120000", 5);
    store.begin("2026-11-15-090000", 6);
    EXPECT_TRUE(fs::exists(d.path / "2026-11-16-120000-5.journal"));
    EXPECT_TRUE(fs::exists(d.path / "2026-11-16-120000-5-2.journal"));
    EXPECT_EQ(store.latest()->journal.filename(), "2026-11-16-120000-5-2.journal");
}

TEST(RiftboundGameStore, AJournalThatDoesntFitIsSetAside) {
    TempDir d;
    game::GameStore store(d.path);
    StoredGame g(store);
    ASSERT_TRUE(g.host.command({{"action", "new"}, {"buddy", "vi"}, {"dev", "greedy"}, {"seed", 23}}).ok);
    g.absorb();
    g.play(60);
    const fs::path journal = d.path / "2026-11-16-120000-23.journal";
    std::ofstream(journal, std::ios::app) << R"({"ai":{"id":999,"action":"pass"}})" << "\n";
    auto r = game::resume_latest(store, db(), host_config());
    EXPECT_EQ(r.host, nullptr);
    EXPECT_NE(r.note.find("set aside"), std::string::npos) << r.note;
    EXPECT_FALSE(fs::exists(journal));
    EXPECT_TRUE(fs::exists(fs::path(journal).concat(".broken")));
    EXPECT_FALSE(store.latest());

    // a replay file that isn't the game's
    StoredGame h(store);
    ASSERT_TRUE(h.host.command({{"action", "new"}, {"buddy", "vi"}, {"dev", "greedy"}, {"seed", 24}}).ok);
    h.absorb();
    h.play(60);
    std::ofstream(d.path / "2026-11-16-120000-24.txt", std::ios::trunc) << "seed 99\n";
    r = game::resume_latest(store, db(), host_config());
    EXPECT_EQ(r.host, nullptr);
    EXPECT_NE(r.note.find("replay"), std::string::npos) << r.note;
}
