// The command host (#50): bot games through the host alone, a human Dev (a SimDev outside) playing only by the
// payload's options and prompts, and every command the host must refuse without changing anything.
#include <gtest/gtest.h>

#include <cstdlib>

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
    std::vector<std::string> voice, replay, problems;
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
            if (!states.empty() && (*o.state)["v"] != states.back()["v"].get<long>() + 1) problems.push_back("v skipped");
            states.push_back(*o.state);
        }
        voice.insert(voice.end(), o.voice.begin(), o.voice.end());
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
        const auto r = g.host.command({{"action", "new"}, {"buddy", buddy}, {"dev", dev}, {"seed", 3}});
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
            if (o.named && o.card && shown["card"] != o.card->home().code.str()) g.problems.push_back("card " + shown.dump());
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

    ASSERT_TRUE(g.host.command({{"action", "new"}, {"buddy", "vi"}, {"dev", "human"}}).ok);  // seed from random_seed
    g.dev = [] { return false; };
    g.run();
    ASSERT_TRUE(g.host.session()->status() == Status::DevToReveal);  // Dev's battlefield first
    EXPECT_EQ(g.host.session()->config().seed, 42u);
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
