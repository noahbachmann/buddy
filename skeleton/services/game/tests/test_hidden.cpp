// Hidden information (108, 128): who knows which card (Object::known), view(), determinize().
#include <gtest/gtest.h>

#include <set>

#include "test_cards.hpp"
#include "test_hidden.hpp"

using namespace buddy::riftbound;
using test::check_view;

namespace {

const std::string kDir = std::string(BUDDY_SOURCE_DIR) + "/services/game";

const CardDb& fixture() {
    static const CardDb db = CardDb::load(kDir + "/tests/data/cards.json");
    return db;
}

const Deck& vi() {
    static const Deck d = load_deck(fixture(), kDir + "/decks/vi-unleashed.txt");
    return d;
}

const Deck& zed() {
    static const Deck d = load_deck(fixture(), kDir + "/decks/zed-vendetta.txt");
    return d;
}

bool hidden(const GameState& v, ObjId id) { return v.obj(id).card == hidden_card(); }

} // namespace

TEST(RiftboundHidden, ViewAtTheStart) {  // 108.4.e, 108.5.d, 108.7.c, 128.4, 128.5
    const GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    for (PlayerId p = 0; p < player_count; ++p) {
        const GameState v = view(s, p);
        const Player &me = v.player(p), &opp = v.player(opponent(p));
        for (const ObjId id : me.hand) EXPECT_FALSE(hidden(v, id));
        for (const ObjId id : opp.hand) EXPECT_TRUE(hidden(v, id));
        for (const Player* pl : {&me, &opp}) {
            for (const ObjId id : pl->main_deck) EXPECT_TRUE(hidden(v, id));
            for (const ObjId id : pl->rune_deck) EXPECT_TRUE(hidden(v, id));
            EXPECT_FALSE(hidden(v, pl->legend.front()));
            EXPECT_FALSE(hidden(v, pl->champion.front()));
        }
        for (const Battlefield& b : v.battlefields) EXPECT_FALSE(hidden(v, b.card));
        EXPECT_EQ(v.rng, Rng{});  // the next shuffle is secret too
    }
}

TEST(RiftboundHidden, IdsSayNothingAboutCards) {  // setup creates deck objects in shuffled order
    std::set<const Card*> first;
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        const GameState s = new_game(vi(), zed(), {.seed = seed, .vanilla = true});
        first.insert(s.obj(s.player(0).legend.front() + 2).card);  // legend, champion, then the deck's first object
    }
    EXPECT_GT(first.size(), 3u);
}

TEST(RiftboundHidden, ACardSeenStaysKnown) {  // 128.4: returned to the hand, the opponent still knows it
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    const ObjId id = s.player(0).main_deck.back();
    move_to(s, id, Zone::Base, 0);
    move_to(s, id, Zone::Hand, 0);
    EXPECT_FALSE(hidden(view(s, 1), id));
    move_to(s, id, Zone::MainDeck, 0, 0, Pos::Bottom);
    EXPECT_FALSE(hidden(view(s, 1), id));  // its place in the deck is known too
}

TEST(RiftboundHidden, RecyclingSeveralForgetsWhichIsWhich) {  // 417.5
    GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    const auto hand = s.player(0).hand;
    recycle(s, {hand[0]});
    EXPECT_FALSE(hidden(view(s, 0), hand[0]));  // one card: the owner knows it's at the bottom
    recycle(s, {hand[1], hand[2]});
    EXPECT_TRUE(hidden(view(s, 0), hand[1]));
    EXPECT_TRUE(hidden(view(s, 0), hand[2]));
}

TEST(RiftboundHidden, DeterminizeKeepsTheViewAndShufflesTheRest) {
    const GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    Rng rng(9);
    EXPECT_EQ(check_view(s, vi(), zed(), rng, true), "");
    const GameState v = view(s, 0);
    const GameState a = determinize(v, vi(), zed(), rng), b = determinize(v, vi(), zed(), rng);
    EXPECT_NE(a.player(1).hand, std::vector<ObjId>{});
    EXPECT_FALSE(a == b);  // different samples
    for (const ObjId id : s.player(0).hand) EXPECT_EQ(a.obj(id).card, s.obj(id).card);
}

TEST(RiftboundHidden, DeterminizeRefusesAViewOfOtherDecks) {
    const GameState s = new_game(vi(), zed(), {.seed = 4, .vanilla = true});
    Rng rng(1);
    EXPECT_THROW(determinize(view(s, 0), zed(), vi(), rng), std::logic_error);
}

TEST(RiftboundHidden, ViewsAlongRandomGames) {  // real decks with and without rules, spell decks; both seats' views
    for (std::uint64_t seed = 0; seed < 6; ++seed) {
        EXPECT_EQ(test::views_along_a_game(vi(), zed(), seed, true), "");
        EXPECT_EQ(test::views_along_a_game(vi(), zed(), seed, false), "");
    }
    const Deck a = test::spell_deck("A"), b = test::spell_deck("B");
    for (std::uint64_t seed = 0; seed < 4; ++seed) {
        EXPECT_EQ(test::views_along_a_game(a, b, seed, true), "");
    }
}
