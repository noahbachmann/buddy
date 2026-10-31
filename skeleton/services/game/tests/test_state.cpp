// GameState primitives: zone moves and new objects (124, 056), recycle (417), the RNG, the invariant check.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <set>

#include "riftbound/engine.hpp"
#include "test_cards.hpp"

using namespace buddy::riftbound;

namespace {

GameState game() {
    static const Deck a = test::simple_deck("A"), b = test::simple_deck("B");
    return new_game(a, b, {.seed = 3, .vanilla = true});
}

bool contains(const std::vector<ObjId>& list, ObjId id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

} // namespace

TEST(RiftboundState, BoardMovesKeepTheObject) {  // 124: only non-board zone changes make a new object
    GameState s = game();
    const ObjId id = s.player(0).hand.front();
    move_to(s, id, Zone::Base, 0);
    Object& o = s.obj(id);
    const ObjRef ref = o.ref(id);
    o.damage = 2;
    o.buffs = 1;
    o.exhausted = true;
    move_to(s, id, Zone::Battlefield, 0, 1);
    EXPECT_TRUE(s.alive(ref));
    EXPECT_EQ(s.obj(id).damage, 2);
    EXPECT_TRUE(contains(s.battlefields[1].here, id));
    EXPECT_FALSE(contains(s.player(0).base, id));
    EXPECT_TRUE(check_invariants(s).empty());

    move_to(s, id, Zone::Trash, 0);
    EXPECT_FALSE(s.alive(ref));
    EXPECT_EQ(s.obj(id).damage, 0);
    EXPECT_EQ(s.obj(id).buffs, 0);
    EXPECT_FALSE(s.obj(id).exhausted);
    EXPECT_TRUE(contains(s.player(0).trash, id));
    EXPECT_TRUE(check_invariants(s).empty());
}

TEST(RiftboundState, NonBoardZonesAreTheOwners) {  // 056, 107.1.c
    GameState s = game();
    const ObjId id = s.player(0).hand.front();
    move_to(s, id, Zone::Base, 1);  // player 1 controls player 0's card
    EXPECT_TRUE(contains(s.player(1).base, id));
    EXPECT_EQ(s.obj(id).controller, 1);
    move_to(s, id, Zone::Hand, 1);  // "to its controller's hand" still lands in the owner's
    EXPECT_TRUE(contains(s.player(0).hand, id));
    EXPECT_EQ(s.obj(id).controller, 0);
    EXPECT_TRUE(check_invariants(s).empty());
}

TEST(RiftboundState, RecycleRunesToTheRuneDeck) {  // 417.1.b
    GameState s = game();
    const ObjId rune = s.player(1).rune_deck.back();
    move_to(s, rune, Zone::Base, 1);
    const ObjId card = s.player(1).hand.front();
    move_to(s, card, Zone::Trash, 1);
    recycle(s, {rune, card});
    EXPECT_EQ(s.player(1).rune_deck.front(), rune);
    EXPECT_EQ(s.player(1).main_deck.front(), card);
    EXPECT_TRUE(check_invariants(s).empty());
}

TEST(RiftboundState, InvariantsCatchBrokenZones) {
    GameState s = game();
    s.player(0).hand.push_back(s.player(0).hand.front());  // in the hand twice
    EXPECT_FALSE(check_invariants(s).empty());

    s = game();
    s.player(1).main_deck.pop_back();  // in no list
    EXPECT_FALSE(check_invariants(s).empty());

    s = game();
    s.obj(s.player(0).hand.front()).zone = Zone::Trash;  // list and zone disagree
    EXPECT_FALSE(check_invariants(s).empty());
}

TEST(RiftboundRng, DeterministicAndUniform) {
    Rng a(5), b(5), c(6);
    EXPECT_EQ(a.next(), b.next());
    EXPECT_NE(a.next(), c.next());

    // seed 0 gives a well-mixed stream (splitmix64), every bucket hit about equally
    Rng r(0);
    std::array<int, 6> hits{};
    for (int i = 0; i < 60000; ++i) ++hits[r.below(6)];
    for (const int h : hits) EXPECT_NEAR(h, 10000, 400);

    // Fisher–Yates hits every permutation of 3
    std::set<std::vector<int>> perms;
    for (int i = 0; i < 200; ++i) {
        std::vector<int> v{1, 2, 3};
        r.shuffle(v);
        perms.insert(v);
    }
    EXPECT_EQ(perms.size(), 6u);
}

TEST(RiftboundRng, FixedStream) {  // recorded games replay from seed + actions: changing the stream breaks them
    Rng r(42);
    EXPECT_EQ(r.next(), 0x15780b2e0c2ec716u);
    EXPECT_EQ(r.next(), 0x6104d9866d113a7eu);
    EXPECT_EQ(r.next(), 0xae17533239e499a1u);
}
