// Replays (replay.hpp): seed + decklists + actions as text, played back with play_random's checks.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "riftbound/engine.hpp"
#include "riftbound/random_game.hpp"
#include "riftbound/replay.hpp"

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
    return {seed, true, {deck_text("vi-unleashed"), deck_text("zed-vendetta")}, g.log};
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
