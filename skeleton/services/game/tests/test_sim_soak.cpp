// #51's gate in small: the soak's games (game_sim) under the stress faults, SimDev greedy (a true shuffled deck)
// against Buddy greedy, both decks from both seats: 0 unrecovered, 0 desyncs, 0 leaks, every game ends. 100 games in
// 10 tests (ctest runs them in parallel); the full gate is `buddy-game --sim --games 1000 --faults stress`.
#include <gtest/gtest.h>

#include <iostream>
#include <sstream>

#include "session_game.hpp"

namespace {

void soak_games(int first) {
    game::SoakOptions o;
    o.seed = 5100;
    o.faults = table::parse_faults("stress");
    const std::array deck_texts{deck_text("vi-unleashed"), deck_text("zed-vendetta")};
    std::vector<game::SoakGame> games;
    for (int i = first; i < first + 10; ++i) {
        const game::SoakGame g = game::soak_game(db(), deck_texts, o, i);
        SCOPED_TRACE("game " + std::to_string(i) + " (seed " + std::to_string(g.seed) + ")");
        EXPECT_TRUE(g.over);
        EXPECT_TRUE(g.problems.empty()) << g.problems.front();
        EXPECT_TRUE(g.leaks.empty()) << g.leaks.front();
        EXPECT_EQ(g.desyncs, 0);
        EXPECT_GT(g.leak_checks, 10);
        games.push_back(g);
    }
    long injected = 0;
    for (const auto& g : games) injected += g.injected;
    EXPECT_GT(injected, 100);  // the faults did happen
    std::ostringstream report;
    game::print_soak_report(report, games, 0);
    std::cout << "games " << first << "–" << first + 9 << ":\n" << report.str();
}

} // namespace

TEST(RiftboundSimSoak, Games0) { soak_games(0); }
TEST(RiftboundSimSoak, Games1) { soak_games(10); }
TEST(RiftboundSimSoak, Games2) { soak_games(20); }
TEST(RiftboundSimSoak, Games3) { soak_games(30); }
TEST(RiftboundSimSoak, Games4) { soak_games(40); }
TEST(RiftboundSimSoak, Games5) { soak_games(50); }
TEST(RiftboundSimSoak, Games6) { soak_games(60); }
TEST(RiftboundSimSoak, Games7) { soak_games(70); }
TEST(RiftboundSimSoak, Games8) { soak_games(80); }
TEST(RiftboundSimSoak, Games9) { soak_games(90); }
