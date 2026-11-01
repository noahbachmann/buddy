// The Vi deck's cards (5.3, cards/vi_unleashed.cpp): one test per card, the card text in front of it; random games of
// every registered card at the end.
#include <gtest/gtest.h>

#include "riftbound/random_game.hpp"
#include "test_real.hpp"

using namespace buddy::riftbound;
using namespace buddy::riftbound::test;

namespace {

Deck vi_deck() {
    return real_deck("V", {"Rengar, Unseen", "Inferna", "Sharkling", "Towering Pairofant", "Arena Kingpin", "Loyal Poro",
                           "Carrion Dredger", "Crimson Pigeons"},
                     domain::order);
}

GameState vi_game() {
    static const Deck d = vi_deck();
    return started(d);
}

Keywords kw(const GameState& s, ObjId id) { return traits(s, id).keywords; }

} // namespace

// "[Accelerate] [Assault 2] [Deflect] [Ganking]"
TEST(RiftboundVi, RengarUnseen) {
    GameState s = vi_game();
    const ObjId r = put(s, s.first_player, real("Rengar, Unseen"), Zone::Base);
    const Keywords k = kw(s, r);
    EXPECT_TRUE(k.has(Keyword::Accelerate));
    EXPECT_EQ(k.get(Keyword::Assault), 2);
    EXPECT_TRUE(k.has(Keyword::Deflect));
    EXPECT_TRUE(k.has(Keyword::Ganking));
}

// "[Ambush] [Assault 2]"
TEST(RiftboundVi, Inferna) {
    GameState s = vi_game();
    const ObjId i = put(s, s.first_player, real("Inferna"), Zone::Base);
    EXPECT_TRUE(kw(s, i).has(Keyword::Ambush));
    EXPECT_EQ(kw(s, i).get(Keyword::Assault), 2);
}

// "[Accelerate] [Assault 4]": paying Accelerate ([1][Fury]) makes it enter ready.
TEST(RiftboundVi, Sharkling) {
    GameState s = vi_game();
    const PlayerId p = s.first_player;
    fund(s, p, 4, {domain::fury, domain::fury});
    const ObjId shark = put(s, p, real("Sharkling"), Zone::Hand);
    EXPECT_EQ(kw(s, shark).get(Keyword::Assault), 4);
    play(s, shark);
    ASSERT_EQ(s.decision.purpose, Purpose::OptionalCost);
    apply(s, legal_actions(s).front());  // pay Accelerate
    settle(s);
    EXPECT_EQ(s.obj(shark).zone, Zone::Base);
    EXPECT_FALSE(s.obj(shark).exhausted);
    expect_ok(s);
}

// "[Assault] If a unit died this turn, I enter ready."
TEST(RiftboundVi, ToweringPairofant) {
    for (const bool death : {false, true}) {
        GameState s = vi_game();
        const PlayerId p = s.first_player;
        if (death) zap_kill(s, p, put(s, opponent(p), real("Inferna"), Zone::Base));
        fund(s, p, 6);
        const ObjId big = put(s, p, real("Towering Pairofant"), Zone::Hand);
        EXPECT_EQ(kw(s, big).get(Keyword::Assault), 1);
        play(s, big);
        settle(s);
        EXPECT_EQ(s.obj(big).zone, Zone::Base);
        EXPECT_EQ(s.obj(big).exhausted, !death) << death;
        expect_ok(s);
    }
}

// "I enter ready. [E]: Give a unit +3 Might this turn."
TEST(RiftboundVi, ArenaKingpin) {
    GameState s = vi_game();
    const PlayerId p = s.first_player;
    fund(s, p, 5, {});
    const ObjId king = put(s, p, real("Arena Kingpin"), Zone::Hand);
    play(s, king);
    settle(s);
    EXPECT_FALSE(s.obj(king).exhausted);
    apply(s, {ActionKind::Activate, king, 0});  // the only unit: targets itself
    settle(s);
    EXPECT_TRUE(s.obj(king).exhausted);
    EXPECT_EQ(might(s, king), 6);
    expect_ok(s);
}

// "[Deathknell][>] If I didn't die alone, draw 1."
TEST(RiftboundVi, LoyalPoro) {
    for (const bool alone : {true, false}) {
        GameState s = vi_game();
        const PlayerId p = s.first_player, o = opponent(p);
        const ObjId poro = put(s, o, real("Loyal Poro"), Zone::Base);
        if (!alone) put(s, o, real("Inferna"), Zone::Base);
        const auto hand = s.player(o).hand.size();
        zap_kill(s, p, poro);
        EXPECT_EQ(s.obj(poro).zone, Zone::Trash);
        EXPECT_EQ(s.player(o).hand.size(), hand + (alone ? 0 : 1)) << alone;
        expect_ok(s);
    }
}

// "[Deathknell][>] Play a 1 Might Bird unit token with [Deflect] to your base."
TEST(RiftboundVi, CarrionDredger) {
    GameState s = vi_game();
    const PlayerId p = s.first_player, o = opponent(p);
    zap_kill(s, p, put(s, o, real("Carrion Dredger"), Zone::Base));
    ASSERT_EQ(s.player(o).base.size(), 1u);
    const ObjId bird = s.player(o).base.front();
    EXPECT_EQ(s.obj(bird).card->name, "Bird");
    EXPECT_EQ(might(s, bird), 1);
    EXPECT_TRUE(kw(s, bird).has(Keyword::Deflect));
    expect_ok(s);
}

// "I have +2 Might while I'm attacking with another unit."
TEST(RiftboundVi, CrimsonPigeons) {
    GameState s = vi_game();
    const PlayerId p = s.first_player;
    const ObjId pigeons = put(s, p, real("Crimson Pigeons"), Zone::Battlefield, 0);
    const ObjId other = put(s, p, real("Inferna"), Zone::Battlefield, 0);
    EXPECT_EQ(might(s, pigeons), 3);
    s.obj(pigeons).designation = Designation::Attacker;
    EXPECT_EQ(might(s, pigeons), 3);  // attacking alone
    s.obj(other).designation = Designation::Attacker;
    EXPECT_EQ(might(s, pigeons), 5);
    s.obj(other).designation = Designation::Defender;  // not "with" it
    EXPECT_EQ(might(s, pigeons), 3);
}

TEST(RiftboundRandom, ViCards) {  // every card registered so far, invariants after every action
    const Deck d = registered_deck("V", cards::vi_unleashed(), domain::order);
    int triggers = 0;
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        const RandomGame g = play_random(d, d, seed, 100000, false);
        ASSERT_TRUE(g.problem.empty()) << "seed " << seed << ": " << g.problem;
        EXPECT_TRUE(g.winner.has_value());
        triggers += g.triggers;
    }
    EXPECT_GT(triggers, 40);
}
