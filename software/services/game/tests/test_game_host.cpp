// The command host (#50): bot games through the host alone, a human Dev (a SimDev outside) playing only by the
// payload's options and prompts, and every command the host must refuse without changing anything.
#include <gtest/gtest.h>

#include <cstdlib>
#include <sstream>

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

// The service's loop around a host: table commands to an in-process table, AI requests to greedy, the payloads
// recorded. A human Dev answers through `dev` (none: wait).
struct HostGame {
    table::Sim sim{{}, {}, 1};
    table::Table table{sim, {}};
    game::GameHost host{db(), host_config()};
    ai::GreedyPlayer buddy_ai{5};
    std::vector<json> states;
    std::vector<std::string> voice, replay, journal, problems;
    int new_games = 0;
    std::function<bool()> dev;  // DevToAct / DevToReveal: send a command; false = stop

    void take() {
        auto o = host.take();
        if (o.new_game) {
            ++new_games;
            const auto& cfg = host.session()->config();
            buddy_ai = ai::GreedyPlayer(5);
            buddy_ai.begin(cfg.buddy, cfg.decks[0], cfg.decks[1]);
        }
        if (o.state) {
            const long want = o.new_game || states.empty() ? 1 : states.back()["v"].get<long>() + 1;  // per game
            if ((*o.state)["v"] != want) problems.push_back("v skipped");
            states.push_back(*o.state);
        }
        voice.insert(voice.end(), o.voice.begin(), o.voice.end());
        journal.insert(journal.end(), o.journal.begin(), o.journal.end());
        if (o.replay_text) {  // an undo: the whole replay again
            replay.clear();
            std::istringstream in(*o.replay_text);
            for (std::string l; std::getline(in, l);) replay.push_back(l);
        }
        replay.insert(replay.end(), o.replay.begin(), o.replay.end());
        pending_table.insert(pending_table.end(), o.table.begin(), o.table.end());
        if (o.ai) pending_ai = std::move(o.ai);
    }

    void run() {
        for (long guard = 0; guard < 2'000'000; ++guard) {
            take();
            if (!pending_table.empty()) {
                const auto c = pending_table.front();
                pending_table.erase(pending_table.begin());
                if (c.kind == CommandKind::Setup) sim.configure({c.sim_deck, c.sim_runes, c.legend, c.champion, true});
                for (const auto& e : table.execute(c)) host.on_table(e);
                continue;
            }
            if (pending_ai) {
                const auto r = std::move(*pending_ai);
                pending_ai.reset();
                host.on_ai(r.id, buddy_ai.choose(r.view, r.actions));
                continue;
            }
            const auto st = host.session()->status();
            if (st == Status::Over || st == Status::Paused) return;
            if ((st == Status::DevToAct || st == Status::DevToReveal) && dev && dev()) continue;
            if (st != Status::DevToAct && st != Status::DevToReveal) problems.push_back("stuck");
            return;
        }
    }

    std::vector<table::Command> pending_table;
    std::optional<game::AiRequest> pending_ai;
};

bool said(const HostGame& g, const std::string& part) {
    return std::any_of(g.voice.begin(), g.voice.end(), [&](const auto& v) { return v.find(part) != std::string::npos; });
}

} // namespace

TEST(RiftboundGameHost, BotGamesPlayToTheEnd) {
    for (const auto& [buddy, dev] : {std::pair{"vi", "greedy"}, std::pair{"zed", "random"}}) {
        HostGame g;
        const auto r = g.host.command({{"action", "new"}, {"buddy", buddy}, {"dev", dev}, {"seed", 3}, {"voice", true}});
        ASSERT_TRUE(r.ok) << r.message;
        g.run();
        SCOPED_TRACE(std::string(buddy) + " vs " + dev);
        EXPECT_TRUE(g.problems.empty()) << g.problems.front();
        EXPECT_EQ(g.host.session()->status(), Status::Over);
        EXPECT_EQ(g.new_games, 1);
        ASSERT_FALSE(g.states.empty());
        EXPECT_EQ(g.states.front()["status"], "setup");
        EXPECT_EQ(g.states.back()["status"], "over");
        EXPECT_FALSE(g.states.back()["winner"].is_null());
        EXPECT_EQ(g.states.back(), g.host.state());
        EXPECT_GT(g.states.back()["catalogue"].size(), 40u);
        EXPECT_NE(std::find(g.replay.begin(), g.replay.end(), "seed 3"), g.replay.end());
        EXPECT_TRUE(said(g, "Let's play"));
        EXPECT_TRUE(said(g, " win"));
        // nothing for Dev to do in a bot game: no payload ever waits on Dev
        for (const auto& s : g.states) EXPECT_FALSE(s.contains("options") || s.contains("prompt")) << s["status"];
    }
}

// Dev plays only through the payload: the option at `i` and the prompt's candidates.
TEST(RiftboundGameHost, HumanDevPlaysByCommands) {
    HostGame g;
    ASSERT_TRUE(g.host.command({{"action", "new"}, {"buddy", "zed"}, {"dev", "human"}, {"seed", 4}}).ok);
    const auto& cfg = g.host.session()->config();
    game::SimDev dev(1, std::make_unique<ai::GreedyPlayer>(8), 9);
    dev.begin(g.host.session()->state(), cfg.decks[0], cfg.decks[1]);
    int acts = 0, reveals = 0;
    bool dumped = false;
    g.dev = [&] {
        const Session& s = *g.host.session();
        const json& st = g.host.state();
        if (st["v"] != g.host.v()) g.problems.push_back("state isn't the last payload");
        if (s.status() == Status::DevToAct) {
            const auto o = dev.choose(s.state(), s.dev_options());
            const auto& list = s.dev_options();
            const auto i = static_cast<std::size_t>(std::find(list.begin(), list.end(), o) - list.begin());
            const json& shown = st["options"].at(i);
            if (shown["i"] != i || shown["text"].get<std::string>().empty()) g.problems.push_back("option " + shown.dump());
            if (o.card && shown["card"] != o.card->home().code.str()) g.problems.push_back("card " + shown.dump());
            if (!dumped && s.state().turn >= 6 && std::getenv("BUDDY_GAME_STATE_DUMP")) {  // for the issue's review
                std::ofstream(std::getenv("BUDDY_GAME_STATE_DUMP")) << st.dump(2) << "\n";
                dumped = true;
            }
            ++acts;
            const auto r = g.host.command({{"action", "act"}, {"v", g.host.v()}, {"i", i}});
            if (!r.ok) g.problems.push_back("act refused: " + r.message);
            return r.ok;
        }
        const auto cards = dev.reveal(*s.prompt());
        json codes = json::array();
        for (const Card* c : cards) {
            const std::string code = c->home().code.str();
            const auto& cand = st["prompt"]["candidates"];
            if (std::none_of(cand.begin(), cand.end(), [&](const json& e) { return e["code"] == code; })) {
                g.problems.push_back(c->name + " isn't a candidate");
            }
            codes.push_back(code);
        }
        if (st["prompt"]["n"] != cards.size()) g.problems.push_back("prompt n");
        ++reveals;
        const auto r = g.host.command({{"action", "reveal"}, {"v", g.host.v()}, {"cards", codes}});
        if (!r.ok) g.problems.push_back("reveal refused: " + r.message);
        return r.ok;
    };
    g.run();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.host.session()->status(), Status::Over);
    EXPECT_GT(acts, 30);
    EXPECT_GT(reveals, 3);
}

// The payload without what an undo must change (#97)
json comparable(json st) {
    st.erase("v");
    st.erase("undo");
    return st;
}

// Dev takes back steps that sent Buddy nothing (#97): the payload is the one before that step again, `v` moves on, a
// late AI answer to a dropped question is ignored, and the journal with its undos restores the same game.
TEST(RiftboundGameHost, DevUndoesOwnSteps) {
    HostGame g;
    ASSERT_TRUE(g.host.command({{"action", "new"}, {"buddy", "vi"}, {"dev", "human"}, {"seed", 11}}).ok);
    const auto& cfg = g.host.session()->config();
    game::SimDev dev(1, std::make_unique<ai::GreedyPlayer>(3), 4);
    dev.begin(g.host.session()->state(), cfg.decks[0], cfg.decks[1]);
    json before;          // the payload before Dev's last step
    bool fresh = false;   // that step isn't undone yet
    int undos = 0, chances = 0;
    g.dev = [&] {
        const Session& s = *g.host.session();
        if (g.host.state()["undo"] != g.host.can_undo()) g.problems.push_back("payload undo != can_undo");
        if (fresh && undos < 8 && g.host.can_undo() && ++chances % 2 == 0) {  // each undo replays the game: a few
            const auto v = g.host.v();
            const auto r = g.host.command({{"action", "undo"}, {"v", v}});
            if (!r.ok) g.problems.push_back("undo refused: " + r.message);
            g.take();
            if (g.host.v() != v + 1) g.problems.push_back("v didn't move on");
            if (comparable(g.host.state()) != comparable(before)) g.problems.push_back("undo didn't restore the payload");
            ++undos;
            fresh = false;
            return true;
        }
        before = g.host.state();
        fresh = true;
        if (s.status() == Status::DevToAct) {
            const auto o = dev.choose(s.state(), s.dev_options());
            const auto& list = s.dev_options();
            const auto i = static_cast<std::size_t>(std::find(list.begin(), list.end(), o) - list.begin());
            return g.host.command({{"action", "act"}, {"v", g.host.v()}, {"i", i}}).ok;
        }
        json codes = json::array();
        for (const Card* c : dev.reveal(*s.prompt())) codes.push_back(c->home().code.str());
        return g.host.command({{"action", "reveal"}, {"v", g.host.v()}, {"cards", codes}}).ok;
    };
    g.run();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_EQ(g.host.session()->status(), Status::Over);
    EXPECT_GT(undos, 5);

    std::string replay;
    for (const auto& l : g.replay) replay += l + "\n";
    EXPECT_EQ(replay, g.host.session()->replay_text());  // the file as the service would have it after the undos

    game::GameHost again{db(), host_config()};
    again.restore(g.journal);
    EXPECT_EQ(again.state(), g.host.state());
}

TEST(RiftboundGameHost, UndoWhileBuddyThinksDropsTheQuestion) {
    HostGame g;
    ASSERT_TRUE(g.host.command({{"action", "new"}, {"buddy", "zed"}, {"dev", "human"}, {"seed", 4}}).ok);
    const auto& cfg = g.host.session()->config();
    game::SimDev dev(1, std::make_unique<ai::GreedyPlayer>(8), 9);
    dev.begin(g.host.session()->state(), cfg.decks[0], cfg.decks[1]);
    bool checked = false, refused = false;
    g.dev = [&] {
        const Session& s = *g.host.session();
        if (!g.host.can_undo() && !refused) {  // nothing of Dev's to take back (start, or Buddy moved since)
            refused = !g.host.command({{"action", "undo"}, {"v", g.host.v()}}).ok;
            if (!refused) g.problems.push_back("undo taken without a step to undo");
            g.take();
        }
        if (s.status() == Status::DevToReveal) {
            json codes = json::array();
            for (const Card* c : dev.reveal(*s.prompt())) codes.push_back(c->home().code.str());
            return g.host.command({{"action", "reveal"}, {"v", g.host.v()}, {"cards", codes}}).ok;
        }
        const auto o = dev.choose(s.state(), s.dev_options());
        const auto& list = s.dev_options();
        const auto i = static_cast<std::size_t>(std::find(list.begin(), list.end(), o) - list.begin());
        const json before = g.host.state();
        if (!g.host.command({{"action", "act"}, {"v", g.host.v()}, {"i", i}}).ok) return false;
        g.take();
        if (checked || !g.pending_ai || !g.host.can_undo()) return true;
        // Buddy thinks about Dev's step: take it back, the question goes with it
        const auto stale = *g.pending_ai;
        g.pending_ai.reset();
        if (!g.host.command({{"action", "undo"}, {"v", g.host.v()}}).ok) g.problems.push_back("undo refused");
        g.take();
        if (comparable(g.host.state()) != comparable(before)) g.problems.push_back("undo didn't restore the payload");
        g.host.on_ai(stale.id, stale.actions.front());  // its late answer: ignored
        if (g.host.take().state) g.problems.push_back("a dropped question's answer changed the game");
        checked = true;
        return true;
    };
    g.run();
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
    EXPECT_TRUE(checked);
    EXPECT_TRUE(refused);
    EXPECT_EQ(g.host.session()->status(), Status::Over);
}

// Refused: nothing changes (no payload, same v, same state, no outputs).
TEST(RiftboundGameHost, CommandsAreChecked) {
    HostGame g;
    const auto refused = [&](const json& cmd, const std::string& part) {
        const auto v = g.host.v();
        const std::optional<GameState> before =
            g.host.session() ? std::optional(g.host.session()->state()) : std::nullopt;
        const auto r = g.host.command(cmd);
        EXPECT_FALSE(r.ok) << cmd.dump();
        EXPECT_NE(r.message.find(part), std::string::npos) << cmd.dump() << ": " << r.message;
        const auto o = g.host.take();
        EXPECT_FALSE(o.state.has_value()) << cmd.dump();
        EXPECT_TRUE(o.table.empty() && !o.ai && o.voice.empty() && o.replay.empty()) << cmd.dump();
        EXPECT_EQ(g.host.v(), v);
        if (before) {
            EXPECT_TRUE(g.host.session()->state() == *before) << cmd.dump();
        }
    };
    EXPECT_EQ(g.host.state()["status"], "idle");
    refused(json::array(), "JSON object");
    refused({{"buddy", "vi"}}, "missing action");
    refused({{"action", 3}}, "must be a string");
    refused({{"action", "fly"}}, "unknown action");
    refused({{"action", "act"}, {"v", 0}, {"i", 0}}, "no game");
    refused({{"action", "new"}, {"buddy", "vi"}, {"dev", "human"}, {"colour", "red"}}, "unknown field colour");
    refused({{"action", "new"}, {"buddy", "jinx"}, {"dev", "human"}}, "unknown deck");
    refused({{"action", "new"}, {"buddy", "vi"}, {"dev", "cat"}}, "dev must be");
    refused({{"action", "new"}, {"buddy", "vi"}, {"dev", "human"}, {"seed", -1}}, "seed must be");
    refused({{"action", "new"}, {"buddy", "vi"}, {"dev", "human"}, {"seed", "1"}}, "seed must be");

    ASSERT_TRUE(g.host.command({{"action", "new"}, {"buddy", "vi"}, {"dev", "human"}, {"voice", true}}).ok);  // seed from random_seed
    g.dev = [] { return false; };
    g.run();
    ASSERT_TRUE(g.host.session()->status() == Status::DevToReveal);  // Dev's champion first (#98), then the battlefield
    EXPECT_EQ(g.host.session()->config().seed, 42u);
    ASSERT_EQ(g.host.state()["prompt"]["kind"], "champion");
    ASSERT_EQ(g.host.state()["prompt"]["candidates"].size(), 2u);  // Zed: Master of Shadows' two champions
    {
        const auto options = champion_options(g.host.session()->config().decks[1]);
        for (std::size_t i = 0; i < options.size(); ++i) {  // champion_options' order: the decklist's first
            EXPECT_EQ(g.host.state()["prompt"]["candidates"][i]["code"], options[i]->home().code.str());
        }
    }
    const auto v0 = g.host.v();
    refused({{"action", "reveal"}, {"v", v0}, {"cards", {g.host.state()["players"]["dev"]["legend"]["card"]}}},
            "can't be your champion");
    const std::string champion = g.host.state()["prompt"]["candidates"][1]["code"];
    ASSERT_TRUE(g.host.command({{"action", "reveal"}, {"v", v0}, {"cards", {champion}}}).ok);
    g.run();
    EXPECT_EQ(g.host.state()["players"]["dev"]["champion"][0]["card"], champion);
    ASSERT_TRUE(g.host.session()->status() == Status::DevToReveal);
    ASSERT_EQ(g.host.state()["prompt"]["kind"], "battlefield");
    const auto v = g.host.v();
    const auto n = g.host.state()["prompt"]["candidates"].size();
    EXPECT_EQ(n, 3u);
    refused({{"action", "act"}, {"v", v}, {"i", 0}}, "not your decision");
    refused({{"action", "reveal"}, {"v", v - 1}, {"cards", json::array()}}, "moved on");
    refused({{"action", "reveal"}, {"v", v}, {"cards", "OGN-001"}}, "list of codes");
    refused({{"action", "reveal"}, {"v", v}, {"cards", {"XYZ-999"}}}, "XYZ-999");
    refused({{"action", "reveal"}, {"v", v}, {"cards", json::array()}}, "expected 1 card");
    refused({{"action", "reveal"}, {"v", v}, {"cards", {g.host.state()["players"]["dev"]["legend"]["card"]}}}, "can't come from");
    refused({{"action", "reveal"}, {"v", v}}, "cards must be");
    refused({{"action", "resume"}}, "nothing to resume");
    refused({{"action", "resume"}, {"v", v}}, "unknown field v");
    refused({{"action", "new"}, {"buddy", "vi"}, {"dev", "human"}}, "concede first");

    const std::string field = g.host.state()["prompt"]["candidates"][0]["code"];
    ASSERT_TRUE(g.host.command({{"action", "reveal"}, {"v", v}, {"cards", {field}}}).ok);
    g.run();
    ASSERT_EQ(g.host.session()->status(), Status::DevToAct);
    const auto v2 = g.host.v();
    const auto options = g.host.state()["options"].size();
    ASSERT_GT(options, 1u);
    refused({{"action", "act"}, {"v", v2}, {"i", options}}, "no option");
    refused({{"action", "act"}, {"v", v2}, {"i", "0"}}, "i must be");
    refused({{"action", "act"}, {"v", v2}}, "missing i");
    refused({{"action", "act"}, {"v", v2}, {"i", 0}, {"cards", json::array()}}, "unknown field cards");
    refused({{"action", "reveal"}, {"v", v2}, {"cards", json::array()}}, "no card to name");

    ASSERT_TRUE(g.host.command({{"action", "concede"}}).ok);
    g.take();
    EXPECT_EQ(g.states.back()["status"], "over");
    EXPECT_EQ(g.states.back()["winner"], "buddy");
    EXPECT_TRUE(said(g, "You concede"));
    refused({{"action", "concede"}}, "over");
    ASSERT_TRUE(g.host.command({{"action", "new"}, {"buddy", "zed"}, {"dev", "greedy"}, {"seed", 1}}).ok);  // after the end
    g.run();
    EXPECT_EQ(g.host.session()->status(), Status::Over);
    EXPECT_EQ(g.new_games, 2);
    EXPECT_TRUE(g.problems.empty()) << g.problems.front();
}

// Buddy speaks only when asked (`new {voice: true}` or `voice {on}`): off, no line leaves the host, so nothing goes to
// buddy/voice/say and no speech is synthesized. The switch is in the payload and in the journal.
TEST(RiftboundGameHost, VoiceIsOffUnlessAsked) {
    HostGame quiet;
    ASSERT_TRUE(quiet.host.command({{"action", "new"}, {"buddy", "vi"}, {"dev", "greedy"}, {"seed", 3}}).ok);
    quiet.run();
    EXPECT_EQ(quiet.host.session()->status(), Status::Over);
    EXPECT_TRUE(quiet.voice.empty()) << quiet.voice.front();
    EXPECT_EQ(quiet.states.back()["voice"], false);

    HostGame g;
    ASSERT_TRUE(g.host.command({{"action", "new"}, {"buddy", "vi"}, {"dev", "human"}, {"seed", 5}}).ok);
    g.take();
    EXPECT_TRUE(g.voice.empty());
    EXPECT_FALSE(g.host.command({{"action", "voice"}, {"on", false}}).ok);  // off already
    EXPECT_FALSE(g.host.command({{"action", "voice"}, {"on", "yes"}}).ok);
    EXPECT_FALSE(g.host.command({{"action", "voice"}}).ok);
    EXPECT_FALSE(g.host.command({{"action", "new"}, {"buddy", "vi"}, {"dev", "human"}, {"voice", 1}}).ok);
    const auto v = g.host.v();
    ASSERT_TRUE(g.host.command({{"action", "voice"}, {"on", true}}).ok);
    g.take();
    EXPECT_EQ(g.host.v(), v + 1);  // the payload moved on: the page sees its command taken
    EXPECT_EQ(g.states.back()["voice"], true);
    ASSERT_TRUE(g.host.command({{"action", "concede"}}).ok);
    g.take();
    EXPECT_TRUE(said(g, "You concede"));

    std::vector<std::string> journal;  // the switch comes back from the journal (take() after each input, as the service)
    game::GameHost h(db(), host_config());
    for (const json& cmd : {json{{"action", "new"}, {"buddy", "vi"}, {"dev", "human"}, {"seed", 5}},
                            json{{"action", "voice"}, {"on", true}}}) {
        ASSERT_TRUE(h.command(cmd).ok);
        const auto out = h.take();
        journal.insert(journal.end(), out.journal.begin(), out.journal.end());
    }
    game::GameHost fresh(db(), host_config());
    fresh.restore(journal);
    EXPECT_EQ(fresh.state(), h.state());
    EXPECT_EQ(fresh.state()["voice"], true);
}
