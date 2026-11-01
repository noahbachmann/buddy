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
                           "Carrion Dredger", "Crimson Pigeons", "Vault Breaker", "Square Up", "Right of Conquest",
                           "Tactical Retreat", "Upstage Comedy", "Soul Harvest", "Divining Shells", "Serrated Dirk",
                           "Hextech Gauntlets"},
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

// "[Action] Give a unit [Assault 2] and [Ganking] this turn."
TEST(RiftboundVi, VaultBreaker) {
    GameState s = vi_game();
    const PlayerId p = s.first_player;
    const ObjId unit = put(s, p, real("Loyal Poro"), Zone::Base);
    fund(s, p, 0, {domain::fury});
    play(s, in_hand(s, p, real("Vault Breaker")));
    settle(s);
    EXPECT_EQ(kw(s, unit).get(Keyword::Assault), 2);
    EXPECT_TRUE(kw(s, unit).has(Keyword::Ganking));
    expect_ok(s);
}

// "[Repeat] — Discard 1 Give a unit [Assault 4] this turn."
TEST(RiftboundVi, SquareUp) {
    for (const bool repeat : {false, true}) {
        GameState s = vi_game();
        const PlayerId p = s.first_player;
        const ObjId unit = put(s, p, real("Loyal Poro"), Zone::Base);  // the only unit: every target automatic
        fund(s, p, 4);
        const ObjId spell = in_hand(s, p, real("Square Up"));
        const auto hand = s.player(p).hand.size();
        play(s, spell);
        optional_cost(s, repeat);
        settle(s);
        EXPECT_EQ(kw(s, unit).get(Keyword::Assault), repeat ? 8 : 4) << repeat;  // 807.2: adds up
        EXPECT_EQ(s.player(p).hand.size(), hand - (repeat ? 2 : 1));
        expect_ok(s);
    }
}

// "Draw 1, then draw 1 for each battlefield you or allies control."
TEST(RiftboundVi, RightOfConquest) {
    GameState s = vi_game();
    const PlayerId p = s.first_player;
    at_field(s, p, real("Loyal Poro"), 0);
    at_field(s, p, real("Loyal Poro"), 1);
    fund(s, p, 3, {domain::fury});
    const ObjId spell = in_hand(s, p, real("Right of Conquest"));
    const auto hand = s.player(p).hand.size();
    play(s, spell);
    settle(s);
    EXPECT_EQ(s.player(p).hand.size(), hand - 1 + 3);
    expect_ok(s);
}

// "[Reaction] Choose a friendly unit. The next time it would die this turn, heal it, exhaust it, and recall it
// instead."
TEST(RiftboundVi, TacticalRetreat) {
    GameState s = vi_game();
    const PlayerId p = s.first_player;
    const ObjId unit = at_field(s, p, real("Crimson Pigeons"), 0);
    play(s, in_hand(s, p, real("Tactical Retreat")));
    settle(s);
    zap_kill(s, p, unit);
    EXPECT_EQ(s.obj(unit).zone, Zone::Base);  // recalled, not dead
    EXPECT_EQ(s.obj(unit).damage, 0);
    EXPECT_TRUE(s.obj(unit).exhausted);
    zap_kill(s, p, unit);  // only the next time
    EXPECT_EQ(s.obj(unit).zone, Zone::Trash);
    expect_ok(s);
}

// "[Repeat] [2] Ready a unit."
TEST(RiftboundVi, UpstageComedy) {
    for (const bool repeat : {false, true}) {
        GameState s = vi_game();
        const PlayerId p = s.first_player;
        std::array<ObjId, 2> units{put(s, p, real("Loyal Poro"), Zone::Base), put(s, p, real("Inferna"), Zone::Base)};
        std::sort(units.begin(), units.end());
        for (const ObjId u : units) s.obj(u).exhausted = true;
        fund(s, p, 4);
        play(s, in_hand(s, p, real("Upstage Comedy")));
        optional_cost(s, repeat);
        pick(s, units[0]);
        if (repeat) pick(s, units[1]);  // the repeat's own target (820.2)
        settle(s);
        EXPECT_FALSE(s.obj(units[0]).exhausted);
        EXPECT_EQ(s.obj(units[1]).exhausted, !repeat) << repeat;
        expect_ok(s);
    }
}

// "Kill a unit at a battlefield with 3 Might or less."
TEST(RiftboundVi, SoulHarvest) {
    GameState s = vi_game();
    const PlayerId p = s.first_player, o = opponent(p);
    put(s, o, real("Crimson Pigeons"), Zone::Base);         // 3 Might, not at a battlefield
    at_field(s, o, real("Arena Kingpin"), 0);                         // 3 Might
    const ObjId big = at_field(s, o, real("Towering Pairofant"), 1);  // 6 Might
    fund(s, p, 0, {domain::order});
    const ObjId spell = in_hand(s, p, real("Soul Harvest"));
    play(s, spell);  // Kingpin is the only candidate
    settle(s);
    EXPECT_EQ(s.player(o).trash.size(), 1u);
    EXPECT_EQ(s.obj(s.player(o).trash.front()).card->name, "Arena Kingpin");
    EXPECT_EQ(s.obj(big).zone, Zone::Battlefield);
    expect_ok(s);
}

// "[Vision] [Action][>] Kill this, [E]: Give a unit +2 Might this turn."
TEST(RiftboundVi, DiviningShells) {
    GameState s = vi_game();
    const PlayerId p = s.first_player;
    const ObjId unit = put(s, p, real("Loyal Poro"), Zone::Base);
    const ObjId shells = in_hand(s, p, real("Divining Shells"));
    play(s, shells);
    settle(s);  // Vision: keeps the top card (first option)
    EXPECT_EQ(s.obj(shells).zone, Zone::Base);
    apply(s, {ActionKind::Activate, shells, 0});
    settle(s);
    EXPECT_EQ(s.obj(shells).zone, Zone::Trash);  // killed as the cost
    EXPECT_EQ(might(s, unit), 5);
    expect_ok(s);
}

// "[Equip] [Fury]"; printed Might Bonus +0 and effect text [Assault].
TEST(RiftboundVi, SerratedDirk) {
    GameState s = vi_game();
    const PlayerId p = s.first_player;
    const ObjId unit = put(s, p, real("Loyal Poro"), Zone::Base);
    const ObjId dirk = put(s, p, real("Serrated Dirk"), Zone::Base);
    EXPECT_FALSE(kw(s, unit).has(Keyword::Assault));
    fund(s, p, 0, {domain::fury});
    apply(s, {ActionKind::Activate, dirk, 0});
    settle(s);
    ASSERT_EQ(s.obj(dirk).attached, unit);
    EXPECT_EQ(might(s, unit), 3);
    EXPECT_EQ(kw(s, unit).get(Keyword::Assault), 1);
    EXPECT_FALSE(kw(s, dirk).has(Keyword::Assault));  // 718.2: inactive on the gear itself
    expect_ok(s);
}

// "[Equip] [3][A]. This ability's Energy cost is reduced by the Might of the unit you choose."; Might Bonus +3.
TEST(RiftboundVi, HextechGauntlets) {
    GameState s = vi_game();
    const PlayerId p = s.first_player;
    const ObjId unit = put(s, p, real("Crimson Pigeons"), Zone::Base);  // 3 Might: costs [0][A]
    const ObjId gauntlets = put(s, p, real("Hextech Gauntlets"), Zone::Base);
    s.player(p).pool = {};
    for (const ObjId r : s.player(p).base) {
        if (s.obj(r).card->type == CardType::Rune) s.obj(r).exhausted = true;  // no Energy at all
    }
    s.obj(s.player(p).base.front()).exhausted = false;  // one rune: [A] by recycling it
    apply(s, {ActionKind::Activate, gauntlets, 0});
    settle(s);
    ASSERT_EQ(s.obj(gauntlets).attached, unit);
    EXPECT_EQ(might(s, unit), 6);
    expect_ok(s);
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
