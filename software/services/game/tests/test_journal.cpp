// A game survives a restart of the service (#52): the host is thrown away mid-game and rebuilt from its journal,
// the open table command resent (the table answers a repeat once) and the AI asked again. The restored host must be
// the one it replaces (payload with `v`, replay, session), and the game with crashes must be the game without.
#include <gtest/gtest.h>

#include "game_host.hpp"
#include "session_game.hpp"

using nlohmann::json;

namespace {

game::HostConfig host_config() {
    game::HostConfig cfg;
    for (const auto& [key, file] : {std::pair{"vi", "vi-unleashed"}, std::pair{"zed", "zed-vendetta"}}) {
        const std::string text = deck_text(file);
        cfg.decks[key] = {parse_deck(db(), text), text};
    }
    cfg.random_seed = [] { return std::uint64_t{42}; };
    return cfg;
}

struct Crashes {
    int every_input = 0;    // the service dies between two inputs
    int every_command = 0;  // the service dies after the table ran a command, before its answer arrived
    int timeout_at = 0;     // this command never reaches the table: the clock pauses, Dev resumes
    int lost_at = 0;        // the table ran this command, its answer was lost; the table's link comes back
    bool lost_times_out = false;  // … after the clock paused the game
};

// The service's loop around a host, take() after every input, the way main.cpp runs it: table commands to an
// in-process table, AI requests to greedy (seeded by the request id: the same answer when asked again after a
// crash), a human Dev (SimDev outside the host) answering by the payload.
struct ServiceGame {
    ServiceGame(table::SimFaults faults, Crashes crashes) : sim({}, faults, 7), table(sim, {}), crashes(crashes) {}

    table::Sim sim;
    table::Table table;
    Crashes crashes;
    std::unique_ptr<game::GameHost> host = std::make_unique<game::GameHost>(db(), host_config());
    std::unique_ptr<game::SimDev> dev;
    std::vector<std::string> journal, replay, problems;
    std::vector<table::Command> pending;
    std::optional<game::AiRequest> ai;
    json state;
    int inputs = 0, commands = 0, restores = 0, timeouts = 0;

    void absorb(game::HostOutputs o) {
        journal.insert(journal.end(), o.journal.begin(), o.journal.end());
        replay.insert(replay.end(), o.replay.begin(), o.replay.end());
        if (o.state) state = *o.state;
        pending.insert(pending.end(), o.table.begin(), o.table.end());
        if (o.ai) ai = std::move(o.ai);
    }

    void input(const std::function<void()>& f) {
        f();
        absorb(host->take());
        if (crashes.every_input > 0 && ++inputs % crashes.every_input == 0) crash();
    }

    void command(const json& cmd) {
        input([&] {
            if (const auto r = host->command(cmd); !r.ok) problems.push_back(cmd.dump() + " refused: " + r.message);
        });
    }

    // The service dies: what wasn't sent is gone. A fresh host from the journal must be the same host.
    void crash() {
        auto fresh = std::make_unique<game::GameHost>(db(), host_config());
        game::HostOutputs open;
        try {
            open = fresh->restore(journal);
        } catch (const std::exception& e) {
            problems.push_back(std::string("restore: ") + e.what());
            return;
        }
        ++restores;
        const auto* a = host->session();
        const auto* b = fresh->session();
        if (fresh->state() != host->state()) problems.push_back("restored payload differs at v " + std::to_string(host->v()));
        if (open.state != host->state()) problems.push_back("restore's payload isn't the state");
        if (a->replay_text() != b->replay_text()) problems.push_back("restored replay differs");
        if (a->status() != b->status() || a->table_hand() != b->table_hand() || a->stats().commands != b->stats().commands) {
            problems.push_back("restored session differs");
        }
        const bool resend = a->inflight() && a->status() != Status::Paused;
        if (resend == open.table.empty() || (resend && *a->inflight() != open.table.at(0))) {
            problems.push_back("restore's open command isn't the one in flight");
        }
        if (a->ai_request().has_value() != open.ai.has_value()) problems.push_back("restore's AI question");
        host = std::move(fresh);
        pending = std::move(open.table);
        ai = std::move(open.ai);
    }

    riftbound::Action buddy_choice(const game::AiRequest& r) {
        const auto& cfg = host->session()->config();
        ai::GreedyPlayer p(1000 + r.id);
        p.begin(cfg.buddy, cfg.decks[0], cfg.decks[1]);
        return p.choose(r.view, r.actions);
    }

    void run() {
        for (long guard = 0; guard < 2'000'000; ++guard) {
            if (!pending.empty()) {
                const auto c = pending.front();
                pending.erase(pending.begin());
                ++commands;
                if (commands == crashes.timeout_at) {  // lost on the way: the service's clock fires
                    ++timeouts;
                    input([&] { host->table_timeout(); });
                    continue;
                }
                if (c.kind == CommandKind::Setup) sim.configure({c.sim_deck, c.sim_runes, c.legend, c.champion, true});
                const auto events = table.execute(c);
                if (commands == crashes.lost_at) {
                    if (crashes.lost_times_out) input([&] { host->table_timeout(); });
                    const auto again = host->table_reconnected();
                    if (!again || *again != c) problems.push_back("reconnect doesn't resend the command in flight");
                    else for (const auto& e : table.execute(*again)) input([&] { host->on_table(e); });
                    continue;
                }
                if (crashes.every_command > 0 && commands % crashes.every_command == 0) {
                    crash();  // the answer never arrived
                    continue;
                }
                for (const auto& e : events) input([&] { host->on_table(e); });
                continue;
            }
            if (ai) {
                const auto r = std::move(*ai);
                ai.reset();
                const auto a = buddy_choice(r);
                input([&] { host->on_ai(r.id, a); });
                continue;
            }
            const Session& s = *host->session();
            switch (s.status()) {
                case Status::Over: return;
                case Status::Paused:
                    if (!s.resumable()) return;
                    command({{"action", "resume"}});
                    continue;
                case Status::DevToAct: {
                    const auto o = dev->choose(s.state(), s.dev_options());
                    const auto& list = s.dev_options();
                    const auto i = std::find(list.begin(), list.end(), o) - list.begin();
                    command({{"action", "act"}, {"v", host->v()}, {"i", i}});
                    continue;
                }
                case Status::DevToReveal: {
                    json codes = json::array();
                    for (const Card* c : dev->reveal(*s.prompt())) codes.push_back(c->home().code.str());
                    command({{"action", "reveal"}, {"v", host->v()}, {"cards", codes}});
                    continue;
                }
                default:
                    problems.push_back("stuck in " + std::string(game::to_string(s.status())));
                    return;
            }
        }
        problems.push_back("no end");
    }

    void start(const std::string& buddy, const std::string& dev_kind, std::uint64_t seed) {
        command({{"action", "new"}, {"buddy", buddy}, {"dev", dev_kind}, {"seed", seed}});
        if (dev_kind == "human") {
            const auto& cfg = host->session()->config();
            dev = std::make_unique<game::SimDev>(1, std::make_unique<ai::GreedyPlayer>(seed + 8), seed + 9);
            dev->begin(host->session()->state(), cfg.decks[0], cfg.decks[1]);
        }
        run();
    }
};

void same_game(const std::string& buddy, const std::string& dev, std::uint64_t seed, table::SimFaults faults,
               Crashes crashes) {
    ServiceGame plain(faults, {.timeout_at = crashes.timeout_at});
    plain.start(buddy, dev, seed);
    ASSERT_TRUE(plain.problems.empty()) << plain.problems.front();
    ASSERT_EQ(plain.host->session()->status(), Status::Over);

    ServiceGame g(faults, crashes);
    g.start(buddy, dev, seed);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_GT(g.restores, 5);
    EXPECT_EQ(g.host->session()->status(), Status::Over);
    EXPECT_EQ(g.state, plain.state);  // v included
    EXPECT_EQ(g.journal, plain.journal);
    EXPECT_EQ(g.host->session()->replay_text(), plain.host->session()->replay_text());
    EXPECT_EQ(g.table.state(), plain.table.state());  // every command ran once
    EXPECT_EQ(g.sim.compare(g.table.tracker().belief(), g.table.tracker().hand_pile()), "");
}

} // namespace

TEST(RiftboundJournal, BotGameSurvivesRestarts) {
    same_game("vi", "greedy", 11, {}, {.every_input = 41, .every_command = 17});
}

TEST(RiftboundJournal, HumanGameUnderStressSurvivesRestarts) {
    same_game("zed", "human", 12, table::parse_faults("stress"), {.every_input = 29, .every_command = 13});
}

// A timeout pause and Dev's resume are inputs like any other: a restart while paused resumes paused.
TEST(RiftboundJournal, TimeoutPauseSurvivesARestart) {
    ServiceGame probe({}, {.timeout_at = 40});
    probe.start("vi", "human", 13);
    ASSERT_EQ(probe.timeouts, 1);
    EXPECT_NE(std::find(probe.journal.begin(), probe.journal.end(), R"({"table_lost":"timeout"})"), probe.journal.end());
    same_game("vi", "human", 13, {}, {.every_input = 31, .timeout_at = 40});
}

// The journal holds one game: a second game in the same host restores to the same `v`.
TEST(RiftboundJournal, VCountsPerGame) {
    ServiceGame g({}, {});
    g.start("vi", "greedy", 16);
    ASSERT_EQ(g.host->session()->status(), Status::Over);
    g.journal.clear();
    g.start("zed", "greedy", 17);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    game::GameHost fresh(db(), host_config());
    fresh.restore(g.journal);
    EXPECT_EQ(fresh.state(), g.host->state());
}

TEST(RiftboundJournal, ARestartedTableIsInTheJournal) {
    ServiceGame g({}, {});
    g.command({{"action", "new"}, {"buddy", "zed"}, {"dev", "greedy"}, {"seed", 14}});
    for (int i = 0; i < 30 && !g.pending.empty(); ++i) {  // a few steps
        const auto c = g.pending.front();
        g.pending.erase(g.pending.begin());
        if (c.kind == CommandKind::Setup) g.sim.configure({c.sim_deck, c.sim_runes, c.legend, c.champion, true});
        for (const auto& e : g.table.execute(c)) g.input([&] { g.host->on_table(e); });
        if (g.ai) {
            const auto r = std::move(*g.ai);
            g.ai.reset();
            const auto a = g.buddy_choice(r);
            g.input([&] { g.host->on_ai(r.id, a); });
        }
    }
    g.input([&] { g.host->table_restarted(); });
    g.input([&] { g.host->table_restarted(); });  // once
    EXPECT_EQ(std::count(g.journal.begin(), g.journal.end(), R"({"table_lost":"restart"})"), 1);
    g.crash();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.host->session()->pause(), Pause::TableRestart);
    EXPECT_TRUE(g.pending.empty());
    EXPECT_FALSE(g.host->command({{"action", "resume"}}).ok);
}

// The table lost the broker for a while (frozen, broker restarted) and its answer with it: the reconnect resends the
// command, the table answers the repeat, the game goes on as if nothing happened (no journal line). After a timeout
// pause the repeat's answer is the late answer: the pause ends by itself.
TEST(RiftboundJournal, AReconnectedTableIsAskedAgain) {
    ServiceGame plain({}, {});
    plain.start("vi", "human", 18);
    ServiceGame g({}, {.lost_at = 30});
    g.start("vi", "human", 18);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.state, plain.state);
    EXPECT_EQ(g.journal, plain.journal);
    EXPECT_EQ(g.table.state(), plain.table.state());

    ServiceGame t({}, {.lost_at = 30, .lost_times_out = true});
    t.start("vi", "human", 18);
    EXPECT_TRUE(t.problems.empty()) << t.problems.front();
    EXPECT_EQ(t.host->session()->status(), Status::Over);
    EXPECT_EQ(t.host->session()->replay_text(), plain.host->session()->replay_text());
    EXPECT_EQ(t.table.state(), plain.table.state());
}

// Seeds use all 64 bits: one ≥ 2^63 (half of the service's random seeds) must come back from the journal.
TEST(RiftboundJournal, ASeedAboveInt64Restores) {
    auto cfg = host_config();
    cfg.random_seed = [] { return (std::uint64_t{1} << 63) + 5; };
    game::GameHost host(db(), cfg);
    ASSERT_TRUE(host.command({{"action", "new"}, {"buddy", "vi"}, {"dev", "human"}}).ok);
    const auto out = host.take();
    ASSERT_FALSE(out.journal.empty());
    EXPECT_NE(out.journal.front().find("9223372036854775813"), std::string::npos) << out.journal.front();
    game::GameHost fresh(db(), host_config());
    EXPECT_NO_THROW(fresh.restore(out.journal));
    EXPECT_EQ(fresh.state(), host.state());
    game::GameHost typed(db(), host_config());  // the same seed typed on the bus: parsed JSON, unsigned
    EXPECT_TRUE(typed.command(json::parse(R"({"action":"new","buddy":"vi","dev":"human","seed":9223372036854775813})")).ok);
    EXPECT_FALSE(typed.command(json::parse(R"({"action":"new","buddy":"vi","dev":"human","seed":-1})")).ok);
}

// A journal that doesn't fit the game: restore throws, naming the line.
TEST(RiftboundJournal, ADamagedJournalIsRefused) {
    ServiceGame g({}, {});
    g.start("vi", "human", 15);
    ASSERT_TRUE(g.problems.empty()) << g.problems.front();
    const auto restore = [](std::vector<std::string> journal) -> std::string {
        game::GameHost h(db(), host_config());
        try {
            h.restore(journal);
        } catch (const game::JournalError& e) {
            return e.what();
        }
        return "";
    };
    EXPECT_EQ(restore(g.journal), "");
    EXPECT_EQ(restore({}), "");  // no game: idle

    auto j = g.journal;
    const auto ai = std::find_if(j.begin(), j.end(), [](const std::string& l) { return l.starts_with(R"({"ai")"); });
    ASSERT_NE(ai, j.end());
    j.erase(ai);  // a line lost
    EXPECT_NE(restore(j), "");

    j = g.journal;
    const auto act = std::find_if(j.begin(), j.end(), [](const std::string& l) { return l.find(R"("act")") != std::string::npos; });
    ASSERT_NE(act, j.end());
    json line = json::parse(*act);
    line["cmd"]["v"] = line["cmd"]["v"].get<int>() + 1;  // another version: refused now
    *act = line.dump();
    EXPECT_NE(restore(j).find("refused now"), std::string::npos);

    EXPECT_NE(restore({"{}"}).find("journal line 1"), std::string::npos);
    EXPECT_NE(restore({R"({"what": 1})"}).find("unknown entry"), std::string::npos);
    EXPECT_NE(restore({"not json"}).find("journal line 1"), std::string::npos);
}
