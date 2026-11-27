// Replays (replay.hpp): seed + decklists + actions as text, played back with play_random's checks.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "riftbound/engine.hpp"
#include "riftbound/random_game.hpp"
#include "riftbound/replay.hpp"
#include "test_hidden.hpp"

using namespace buddy::riftbound;

namespace {

const std::string kDir = std::string(BUDDY_SOURCE_DIR) + "/services/game";

const CardDb& fixture() {
    static const CardDb db = CardDb::load(kDir + "/tests/data/cards.json");
    return db;
}

std::string deck_text(const std::string& name) {
    std::ifstream in(kDir + "/decks/" + name + ".txt");
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

Replay replay_of(std::uint64_t seed, const RandomGame& g) {
    return {.seed = seed, .vanilla = true, .decks = {deck_text("vi-unleashed"), deck_text("zed-vendetta")}, .actions = g.log, .buddy = {}, .reveals = {}};
}

} // namespace

TEST(RiftboundReplay, TextRoundTripReplaysTheGame) {
    const Deck vi = parse_deck(fixture(), deck_text("vi-unleashed")), zed = parse_deck(fixture(), deck_text("zed-vendetta"));
    const RandomGame g = play_random(vi, zed, 12);
    ASSERT_EQ(g.problem, "");
    const std::string text = to_text(replay_of(12, g), fixture());
    EXPECT_NE(text.find("# turn 0, player"), std::string::npos) << text.substr(0, 2000);  // annotated
    const Replay back = parse_replay(text);
    EXPECT_EQ(back.seed, 12u);
    EXPECT_TRUE(back.vanilla);
    EXPECT_EQ(back.actions, g.log);
    const ReplayResult r = run_replay(back, fixture());
    EXPECT_EQ(r.problem, "");
    EXPECT_EQ(r.applied, g.actions);
    EXPECT_EQ(r.state.winner, g.winner);
    EXPECT_EQ(r.state.player(0).points, g.points[0]);
}

TEST(RiftboundReplay, ActionSyntax) {
    const Replay r = parse_replay(
        "seed 1\ndeck 0\nLegend:\nend\ndeck 1\nend\nactions\n"
        "pick 12   # set aside\ndone\nactivate 3 1\nmove - 255\npick - 1\nend-turn\n");
    const std::vector<Action> want{{ActionKind::Pick, 12},        {ActionKind::Done},
                                   {ActionKind::Activate, 3, 1},  {ActionKind::Move, no_obj, to_base},
                                   {ActionKind::Pick, no_obj, 1}, {ActionKind::EndTurn}};
    EXPECT_EQ(r.actions, want);
    EXPECT_FALSE(r.vanilla);
    EXPECT_EQ(r.decks[0], "Legend:\n");
}

TEST(RiftboundReplay, MalformedNamesTheLine) {
    const auto error = [](const std::string& text) {
        try {
            parse_replay(text);
        } catch (const ReplayError& e) {
            return std::string(e.what());
        }
        return std::string("no error");
    };
    EXPECT_EQ(error("seed 1\ndeck 0\nend\ndeck 1\nend\nactions\njump 3\n").rfind("line 7: unknown action", 0), 0u);
    EXPECT_EQ(error("seed 1\ndeck 0\nend\ndeck 1\nend\nactions\nplay x\n").rfind("line 7: expected", 0), 0u);
    EXPECT_EQ(error("seed x\n").rfind("line 1:", 0), 0u);
    EXPECT_NE(error("seed 1\ndeck 0\n").find("without \"end\""), std::string::npos);
    EXPECT_NE(error("seed 1\nactions\n").find("needs seed, deck 0 and deck 1"), std::string::npos);
}

TEST(RiftboundReplay, AnIllegalActionIsTheProblem) {
    const Deck vi = parse_deck(fixture(), deck_text("vi-unleashed")), zed = parse_deck(fixture(), deck_text("zed-vendetta"));
    RandomGame g = play_random(vi, zed, 3);
    g.log.resize(10);
    g.log.push_back({ActionKind::Play, 0});  // a legend can't be played
    const ReplayResult r = run_replay(replay_of(3, g), fixture());
    EXPECT_EQ(r.applied, 10);
    EXPECT_NE(r.problem.find("action 10"), std::string::npos) << r.problem;
    const std::string text = to_text(replay_of(3, g), fixture());
    const std::string last = text.substr(text.rfind('\n', text.size() - 2) + 1);
    EXPECT_EQ(last.rfind("# turn", 0), 0u) << last;  // the problem as the last line
    EXPECT_NE(last.find("action 10"), std::string::npos) << last;
}

// 2.6: seed + actions + binds and hand orders reproduce a service game.
TEST(RiftboundReplay, BindsAndOrdersRoundTrip) {
    const Deck vi = parse_deck(fixture(), deck_text("vi-unleashed")), zed = parse_deck(fixture(), deck_text("zed-vendetta"));
    Replay replay{.seed = 21, .vanilla = false, .decks = {deck_text("vi-unleashed"), deck_text("zed-vendetta")},
                  .actions = {}, .buddy = 1, .reveals = {}};
    GameState s = new_game(vi, zed, {.seed = replay.seed, .ask_discard = 1});  // a session's: Dev (seat 0) is asked (#73)
    Rng rng(5);
    BindMemory memory;
    int binds = 0, orders = 0;
    for (int n = 0; !s.winner && n < 3000; ++n) {
        if (rng.below(3) == 0) {
            if (const auto b = test::random_bind(s, 1, rng, rng.below(2) == 0, &memory)) {
                replay.reveals.push_back({.before = replay.actions.size(), .kind = Reveal::Kind::Bind, .obj = b->first,
                                          .code = b->second->home().code.str(), .player = 0, .ids = {}});
                ++binds;
            }
        }
        if (rng.below(8) == 0 && s.player(1).hand.size() > 1) {
            auto hand = s.player(1).hand;
            rng.shuffle(hand);
            ASSERT_TRUE(set_hand_order(s, 1, hand));
            replay.reveals.push_back({.before = replay.actions.size(), .kind = Reveal::Kind::Order, .obj = no_obj,
                                      .code = {}, .player = 1, .ids = hand});
            ++orders;
        }
        const auto actions = legal_actions(s);
        replay.actions.push_back(actions.at(rng.below(actions.size())));
        apply(s, replay.actions.back());
    }
    ASSERT_GT(binds, 20);
    ASSERT_GT(orders, 2);
    const std::string text = to_text(replay, fixture());
    EXPECT_NE(text.find("\nbuddy 1\n"), std::string::npos);
    EXPECT_NE(text.find("\nbind "), std::string::npos);
    EXPECT_NE(text.find("\norder 1 "), std::string::npos);
    const Replay back = parse_replay(text);
    EXPECT_EQ(back.buddy, replay.buddy);
    EXPECT_EQ(back.actions, replay.actions);
    EXPECT_EQ(back.reveals, replay.reveals);
    const ReplayResult r = run_replay(back, fixture());
    EXPECT_EQ(r.problem, "");
    EXPECT_EQ(r.state, s);  // the same game, every card where the live one had it
}

TEST(RiftboundReplay, RevealSyntax) {
    const std::string head = "seed 1\nbuddy 0\ndeck 0\nend\ndeck 1\nend\nactions\n";
    const Replay r = parse_replay(head + "bind 41 OGN-007a  # Vi\ndone\norder 1 3 9 4\n");
    ASSERT_EQ(r.reveals.size(), 2u);
    EXPECT_EQ(r.reveals[0].before, 0u);
    EXPECT_EQ(r.reveals[0].obj, 41);
    EXPECT_EQ(r.reveals[0].code, "OGN-007a");
    EXPECT_EQ(r.reveals[1].before, 1u);
    EXPECT_EQ(r.reveals[1].kind, Reveal::Kind::Order);
    EXPECT_EQ(r.reveals[1].player, 1);
    EXPECT_EQ(r.reveals[1].ids, (std::vector<ObjId>{3, 9, 4}));
    EXPECT_THROW(parse_replay("seed 1\ndeck 0\nend\ndeck 1\nend\nactions\nbind 41 OGN-007a\n"), ReplayError);
    EXPECT_THROW(parse_replay(head + "bind 41\n"), ReplayError);
    EXPECT_THROW(parse_replay(head + "order 2 1\n"), ReplayError);
    EXPECT_THROW(parse_replay(head + "assume 7 OGN-001a\n"), ReplayError);  // #73: Dev's cards are declared, not assumed
}

TEST(RiftboundReplay, AnImpossibleBindIsTheProblem) {
    Replay replay{.seed = 3, .vanilla = true, .decks = {deck_text("vi-unleashed"), deck_text("zed-vendetta")},
                  .actions = {}, .buddy = 0, .reveals = {}};
    const Deck vi = parse_deck(fixture(), deck_text("vi-unleashed")), zed = parse_deck(fixture(), deck_text("zed-vendetta"));
    const GameState s = new_game(vi, zed, {.seed = 3, .vanilla = true});
    const ObjId top = s.player(0).main_deck.back();
    const std::string rune = s.obj(s.player(0).rune_deck.back()).card->home().code.str();
    replay.reveals.push_back({.before = 0, .kind = Reveal::Kind::Bind, .obj = top, .code = rune, .player = 0, .ids = {}});
    const ReplayResult r = run_replay(replay, fixture());
    EXPECT_NE(r.problem.find("bind " + std::to_string(top) + " " + rune), std::string::npos) << r.problem;
}
