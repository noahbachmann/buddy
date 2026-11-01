// The Zed deck's cards (5.3, cards/zed_vendetta.cpp): one test per card, the card text in front of it; random games of
// every registered card at the end.
#include <gtest/gtest.h>

#include "riftbound/random_game.hpp"
#include "test_real.hpp"

using namespace buddy::riftbound;
using namespace buddy::riftbound::test;

namespace {

Deck zed_deck() {
    return real_deck("Z", {"Shadow Assassin", "Shadowblade Lurker", "Kinkou Lifeblade", "Shadow Fiend", "Punching Poro"},
                     domain::chaos);
}

GameState zed_game() {
    static const Deck d = zed_deck();
    return started(d);
}

// A copy of `card` in p's hand: one already there, else one from the deck (put() may take one from the trash).
ObjId in_hand(GameState& s, PlayerId p, const Card* card) {
    for (const ObjId id : s.player(p).hand) {
        if (s.obj(id).card == card) return id;
    }
    for (const ObjId id : s.player(p).main_deck) {
        if (s.obj(id).card != card) continue;
        move_to(s, id, Zone::Hand, p);
        return id;
    }
    throw std::logic_error("no copy in hand or deck");
}

// Activates Empower (ability 0), discarding the first card if asked, and resolves it.
void empower_it(GameState& s, ObjId unit) {
    apply(s, {ActionKind::Activate, unit, 0});
    settle(s);
}

} // namespace

// "I enter ready if you have a card with my name in your trash."
TEST(RiftboundZed, ShadowAssassin) {
    for (const bool in_trash : {false, true}) {
        GameState s = zed_game();
        const PlayerId p = s.first_player;
        if (in_trash) put(s, p, real("Shadow Assassin"), Zone::Trash);
        const ObjId a = in_hand(s, p, real("Shadow Assassin"));
        put(s, opponent(p), real("Shadow Assassin"), Zone::Trash);  // the opponent's trash doesn't count
        fund(s, p, 5);
        play(s, a);
        settle(s);
        EXPECT_EQ(s.obj(a).zone, Zone::Base);
        EXPECT_EQ(s.obj(a).exhausted, !in_trash) << in_trash;
        expect_ok(s);
    }
}

// "I cost [2] less for each card with my name in your trash."
TEST(RiftboundZed, ShadowbladeLurker) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    const ObjId lurker = put(s, p, real("Shadowblade Lurker"), Zone::Hand);
    fund(s, p, -s.player(p).pool.energy);  // only the 2 runes
    EXPECT_FALSE(playable(s, p, lurker));  // [5]
    put(s, p, real("Shadowblade Lurker"), Zone::Trash);
    EXPECT_FALSE(playable(s, p, lurker));  // [3]
    put(s, p, real("Shadowblade Lurker"), Zone::Trash);
    EXPECT_TRUE(playable(s, p, lurker));  // [1]
    play(s, lurker);
    settle(s);
    EXPECT_EQ(s.obj(lurker).zone, Zone::Base);
    expect_ok(s);
}

// "[Empower] [2] [Empowered][>] I have +1 Might and [Ganking]."
TEST(RiftboundZed, KinkouLifeblade) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    const ObjId k = put(s, p, real("Kinkou Lifeblade"), Zone::Base);
    EXPECT_EQ(might(s, k), 4);
    EXPECT_FALSE(has(s, k, Keyword::Ganking));
    empower_it(s, k);
    EXPECT_TRUE(s.obj(k).empowered);
    EXPECT_EQ(might(s, k), 5);
    EXPECT_TRUE(has(s, k, Keyword::Ganking));
    expect_ok(s);
}

// "[Empower] [2][Fury] [Empowered][>] I have [Assault 3]."
TEST(RiftboundZed, ShadowFiend) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    const ObjId f = put(s, p, real("Shadow Fiend"), Zone::Base);
    EXPECT_FALSE(has(s, f, Keyword::Assault));
    fund(s, p, 0, {domain::fury});
    empower_it(s, f);
    EXPECT_TRUE(s.obj(f).empowered);
    EXPECT_EQ(traits(s, f).keywords.get(Keyword::Assault), 3);
    expect_ok(s);
}

// "[Empower] — Discard 1 [Empowered][>] I have +1 Might."
TEST(RiftboundZed, PunchingPoro) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    const ObjId poro = put(s, p, real("Punching Poro"), Zone::Base);
    const auto hand = s.player(p).hand.size();
    empower_it(s, poro);
    EXPECT_EQ(s.player(p).hand.size(), hand - 1);
    EXPECT_EQ(might(s, poro), 3);
    expect_ok(s);
}

TEST(RiftboundRandom, ZedCards) {  // every card registered so far, invariants after every action
    const Deck d = registered_deck("Z", cards::zed_vendetta(), domain::chaos);
    int activations = 0;
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        const RandomGame g = play_random(d, d, seed, 100000, false);
        ASSERT_TRUE(g.problem.empty()) << "seed " << seed << ": " << g.problem;
        EXPECT_TRUE(g.winner.has_value());
        activations += g.activations;
    }
    EXPECT_GT(activations, 40);
}
