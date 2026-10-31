// Random-vs-random games in vanilla mode: invariants after every action, every game ends (#22's "Done when").
#include <gtest/gtest.h>

#include "riftbound/random_game.hpp"
#include "test_cards.hpp"

using namespace buddy::riftbound;

namespace {

const std::string kDir = std::string(BUDDY_SOURCE_DIR) + "/services/game";

const CardDb& fixture() {
    static const CardDb db = CardDb::load(kDir + "/tests/data/cards.json");
    return db;
}

void play_many(const Deck& a, const Deck& b, int games) {
    std::array<int, player_count> wins{};
    for (int seed = 0; seed < games; ++seed) {
        const RandomGame g = play_random(a, b, static_cast<std::uint64_t>(seed));
        ASSERT_TRUE(g.problem.empty()) << "seed " << seed << ": " << g.problem;
        ASSERT_TRUE(g.winner);
        EXPECT_GT(g.turns, 20) << seed;  // vanilla: no scoring yet, games end by burning out
        ++wins[*g.winner];
    }
    EXPECT_GT(wins[0], 0);
    EXPECT_GT(wins[1], 0);
}

} // namespace

TEST(RiftboundRandom, SimpleDecks) {
    play_many(test::simple_deck("A"), test::simple_deck("B"), 50);
}

TEST(RiftboundRandom, RealDecksVanilla) {
    play_many(load_deck(fixture(), kDir + "/decks/vi-unleashed.txt"),
              load_deck(fixture(), kDir + "/decks/zed-vendetta.txt"), 50);
}

TEST(RiftboundRandom, SameSeedSameGame) {
    const Deck a = test::simple_deck("A"), b = test::simple_deck("B");
    const RandomGame x = play_random(a, b, 77), y = play_random(a, b, 77);
    EXPECT_EQ(x.actions, y.actions);
    EXPECT_EQ(x.winner, y.winner);
    EXPECT_EQ(x.points, y.points);
}

TEST(RiftboundRandom, ReportsAGameThatDoesNotEnd) {
    const RandomGame g = play_random(test::simple_deck("A"), test::simple_deck("B"), 1, 10);
    EXPECT_NE(g.problem.find("no winner yet"), std::string::npos) << g.problem;
}
