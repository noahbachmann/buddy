// The Zed deck's cards (5.3, cards/zed_vendetta.cpp): one test per card, the card text in front of it; random games of
// every registered card at the end.
#include <gtest/gtest.h>

#include "riftbound/random_game.hpp"
#include "test_real.hpp"

using namespace buddy::riftbound;
using namespace buddy::riftbound::test;

namespace {

Deck zed_deck() {
    return real_deck("Z", {"Shadow Assassin", "Shadowblade Lurker", "Kinkou Lifeblade", "Shadow Fiend", "Punching Poro",
                            "Ruthless Strike", "Perfect Execution", "Twilight Step", "Wind and Ghosts", "Death Mark"},
                     domain::chaos);
}

GameState zed_game() {
    static const Deck d = zed_deck();
    return started(d);
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

// "[Action] As an additional cost to play this, you may discard 1. Deal 3 to a unit at a battlefield. If you paid
// the additional cost, deal 5 to it instead."
TEST(RiftboundZed, RuthlessStrike) {
    for (const bool paid : {false, true}) {
        GameState s = zed_game();
        const PlayerId p = s.first_player, o = opponent(p);
        const ObjId unit = at_field(s, o, real("Kinkou Lifeblade"), 0);  // 4 Might
        fund(s, p, 3);
        play(s, in_hand(s, p, real("Ruthless Strike")));
        optional_cost(s, paid);
        settle(s);
        EXPECT_EQ(s.obj(unit).zone, paid ? Zone::Trash : Zone::Battlefield) << paid;
        expect_ok(s);
    }
}

// "Ready a unit and give it [Assault 3] this turn. [Flow] [3][Fury]"
TEST(RiftboundZed, PerfectExecution) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    const ObjId unit = put(s, p, real("Punching Poro"), Zone::Base);
    s.obj(unit).exhausted = true;
    const ObjId spell = put(s, p, real("Perfect Execution"), Zone::Trash);  // played for its Flow cost
    fund(s, p, 3, {domain::fury});
    ASSERT_TRUE(playable(s, p, spell));
    play(s, spell);
    settle(s);
    EXPECT_FALSE(s.obj(unit).exhausted);
    EXPECT_EQ(traits(s, unit).keywords.get(Keyword::Assault), 3);
    EXPECT_EQ(s.obj(spell).zone, Zone::Banishment);
    expect_ok(s);
}

// "Move a unit with 3 Might or less. [Flow] [4][Chaos]": the controller picks the destination.
TEST(RiftboundZed, TwilightStep) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    const ObjId small = put(s, p, real("Punching Poro"), Zone::Base);
    put(s, p, real("Kinkou Lifeblade"), Zone::Base);  // 4 Might: not a candidate
    fund(s, p, 0, {domain::chaos});
    play(s, in_hand(s, p, real("Twilight Step")));
    both_pass(s);
    ASSERT_EQ(s.decision.purpose, Purpose::Effect);
    EXPECT_EQ(s.decision.options.size(), s.battlefields.size());  // not "base": it's there
    apply(s, {ActionKind::Pick, s.battlefields[1].card, 1});
    settle(s);
    EXPECT_EQ(s.obj(small).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(small).where, 1);
    expect_ok(s);
}

// "[Action] Choose a unit at a battlefield. If it has 3 Might or less, banish it. Otherwise, return it to its
// owner's hand."
TEST(RiftboundZed, WindAndGhosts) {
    for (const bool small : {true, false}) {
        GameState s = zed_game();
        const PlayerId p = s.first_player, o = opponent(p);
        const ObjId unit = at_field(s, o, real(small ? "Punching Poro" : "Kinkou Lifeblade"), 0);
        fund(s, p, 3, {domain::chaos});
        play(s, in_hand(s, p, real("Wind and Ghosts")));
        settle(s);
        EXPECT_EQ(s.obj(unit).zone, small ? Zone::Banishment : Zone::Hand) << small;
        expect_ok(s);
    }
}

// "[Burn 3]. Play a 0 Might Shadow Clone unit token. [Flow] [1][A][A]"
TEST(RiftboundZed, DeathMark) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    fund(s, p, 0, {domain::fury});
    const ObjId spell = in_hand(s, p, real("Death Mark"));
    const auto trash = s.player(p).trash.size();
    play(s, spell);
    settle(s);  // the token's location: the first option
    EXPECT_EQ(s.player(p).trash.size(), trash + 3 + 1);  // burnt 3, then the spell itself
    int clones = 0;
    for (const Object& o : s.objects) clones += on_board(o.zone) && o.card->name == "Shadow Clone";
    EXPECT_EQ(clones, 1);
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
