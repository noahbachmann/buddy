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
                            "Ruthless Strike", "Perfect Execution", "Twilight Step", "Wind and Ghosts", "Death Mark",
                            "Zed, From the Shadows", "Zed, Without a Sound", "Morgana, Vindictive", "Mel, Defiant Soul",
                            "Blade Twirler", "Shadow Order Disciple", "Traveling Merchant", "Gust Monk",
                            "Twilight Reveler", "Ravenbloom Prefect",
                            "Serrated Dirk"},  // a gear for Ravenbloom Prefect
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

// "Move a unit with 3 Might or less. [Flow] [4][Chaos]": the destination is picked as it's played.
TEST(RiftboundZed, TwilightStep) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    const ObjId small = put(s, p, real("Punching Poro"), Zone::Base);
    put(s, p, real("Kinkou Lifeblade"), Zone::Base);  // 4 Might: not a candidate
    fund(s, p, 0, {domain::chaos});
    play(s, in_hand(s, p, real("Twilight Step")));  // the Poro: the only unit
    ASSERT_EQ(s.decision.purpose, Purpose::Target);
    const std::vector<Action> fields{{ActionKind::Pick, s.battlefields[0].card, 0},
                                     {ActionKind::Pick, s.battlefields[1].card, 1}};
    EXPECT_EQ(legal_actions(s), fields);  // not its base: it's there
    apply(s, fields[1]);
    ASSERT_EQ(s.chain.size(), 1u);  // chosen on the chain, before anyone responds
    EXPECT_EQ(s.chain[0].targets.size(), 2u);
    settle(s);
    EXPECT_EQ(s.obj(small).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(small).where, 1);
    expect_ok(s);
}

// The destination is rechecked: the unit already there by then → no move.
TEST(RiftboundZed, TwilightStepToWhereItIsNow) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    const ObjId small = at_field(s, p, real("Punching Poro"), 0);
    fund(s, p, 0, {domain::chaos});
    play(s, in_hand(s, p, real("Twilight Step")));
    const std::vector<Action> places{{ActionKind::Pick, s.battlefields[1].card, 1}, {ActionKind::Pick, no_obj, to_base}};
    EXPECT_EQ(legal_actions(s), places);
    apply(s, places[1]);  // to base
    recall(s, small);     // gets there first
    settle(s);
    EXPECT_EQ(s.obj(small).zone, Zone::Base);
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

// "You may discard 1 as an additional cost to play me. When you play me, if you paid the additional cost, play a 0
// Might Shadow Clone unit token."
TEST(RiftboundZed, ZedFromTheShadows) {
    for (const bool paid : {false, true}) {
        GameState s = zed_game();
        const PlayerId p = s.first_player;
        fund(s, p, 4, {domain::fury});
        const ObjId zed = in_hand(s, p, real("Zed, From the Shadows"));
        const auto hand = s.player(p).hand.size();
        play(s, zed);
        optional_cost(s, paid);
        settle(s);
        EXPECT_TRUE(on_board(s.obj(zed).zone));
        EXPECT_EQ(s.player(p).hand.size(), hand - (paid ? 2 : 1));
        EXPECT_EQ(count_on_board(s, p, "Shadow Clone"), paid ? 1 : 0) << paid;
        expect_ok(s);
    }
}

// "When I conquer, play a 0 Might Shadow Clone unit token to your base. [Action][>] [1][Chaos]: Move me and a Shadow
// Clone you control to each other's locations."
TEST(RiftboundZed, ZedWithoutASound) {
    GameState s = zed_game();
    const PlayerId p = s.first_player, o = opponent(p);
    at_field(s, o, real("Punching Poro"), 0);
    const ObjId zed = ready_in_base(s, p, real("Zed, Without a Sound"));
    EXPECT_FALSE(offered(s, {ActionKind::Activate, zed, 1}));  // no clone yet
    move_units(s, 0, {zed});
    both_pass(s);
    settle(s);
    ASSERT_EQ(s.battlefields[0].controller, p);
    ASSERT_EQ(count_on_board(s, p, "Shadow Clone"), 1);
    const ObjId clone = s.player(p).base.back();
    ASSERT_EQ(s.obj(clone).card->name, "Shadow Clone");
    fund(s, p, 1, {domain::chaos});
    apply(s, {ActionKind::Activate, zed, 1});  // the clone: the only target
    settle(s);
    EXPECT_EQ(s.obj(zed).zone, Zone::Base);
    EXPECT_EQ(s.obj(clone).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(clone).where, 0);
    expect_ok(s);
}

// "[Ambush] When you play me, deal damage to a unit equal to the damage marked on it."
TEST(RiftboundZed, MorganaVindictive) {
    GameState s = zed_game();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId enemy = put(s, o, real("Kinkou Lifeblade"), Zone::Base);  // 4 Might
    s.obj(enemy).damage = 2;
    fund(s, p, 5, {domain::fury});
    const ObjId morgana = in_hand(s, p, real("Morgana, Vindictive"));
    EXPECT_TRUE(traits(s, morgana).keywords.has(Keyword::Ambush));
    play(s, morgana);
    while (s.decision.purpose != Purpose::Target) apply(s, legal_actions(s).front());
    pick(s, enemy);
    settle(s);
    EXPECT_EQ(s.obj(enemy).zone, Zone::Trash);  // 2 + 2
    expect_ok(s);
}

// "[Empower] — Discard a spell. When I become [Empowered], banish an enemy unit at a battlefield with 3 Might or
// less."
TEST(RiftboundZed, MelDefiantSoul) {
    GameState s = zed_game();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId enemy = at_field(s, o, real("Punching Poro"), 0);
    const ObjId mel = put(s, p, real("Mel, Defiant Soul"), Zone::Base);
    for (const ObjId id : std::vector<ObjId>(s.player(p).hand)) move_to(s, id, Zone::MainDeck, p);
    EXPECT_FALSE(offered(s, {ActionKind::Activate, mel, 0}));  // no spell to discard
    const ObjId spell = in_hand(s, p, real("Ruthless Strike"));
    in_hand(s, p, real("Punching Poro"));  // a unit: not discardable for it
    apply(s, {ActionKind::Activate, mel, 0});
    settle(s);
    EXPECT_EQ(s.obj(spell).zone, Zone::Trash);
    EXPECT_TRUE(s.obj(mel).empowered);
    EXPECT_EQ(s.obj(enemy).zone, Zone::Banishment);
    expect_ok(s);
}

// "The first time I move each turn, choose a player. They [Burn 1]."
TEST(RiftboundZed, BladeTwirler) {
    GameState s = zed_game();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId twirler = ready_in_base(s, p, real("Blade Twirler"));
    const auto trash = s.player(o).trash.size();
    move_units(s, 1, {twirler});
    while (s.decision.purpose != Purpose::Target) apply(s, legal_actions(s).front());
    apply(s, {ActionKind::Pick, no_obj, o});
    close_showdown(s);
    EXPECT_EQ(s.player(o).trash.size(), trash + 1);
    s.obj(twirler).exhausted = false;
    move_units(s, to_base, {twirler});
    EXPECT_TRUE(s.chain.empty());  // only the first time
    EXPECT_TRUE(s.triggered.empty());
    expect_ok(s);
}

// "When I move, you may [Burn 1] to give me +1 Might this turn."
TEST(RiftboundZed, ShadowOrderDisciple) {
    for (const bool pay : {false, true}) {
        GameState s = zed_game();
        const PlayerId p = s.first_player;
        const ObjId disciple = ready_in_base(s, p, real("Shadow Order Disciple"));
        const auto trash = s.player(p).trash.size();
        move_units(s, 1, {disciple});
        ASSERT_EQ(s.decision.purpose, Purpose::May);
        apply(s, pay ? legal_actions(s).front() : Action{ActionKind::Done});
        settle(s);
        EXPECT_EQ(s.player(p).trash.size(), trash + (pay ? 1 : 0));
        EXPECT_EQ(might(s, disciple), pay ? 3 : 2) << pay;
        expect_ok(s);
    }
}

// "When I move, discard 1, then draw 1."
TEST(RiftboundZed, TravelingMerchant) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    const ObjId merchant = ready_in_base(s, p, real("Traveling Merchant"));
    const auto hand = s.player(p).hand.size(), trash = s.player(p).trash.size();
    move_units(s, 1, {merchant});
    settle(s);
    EXPECT_EQ(s.player(p).hand.size(), hand);
    EXPECT_EQ(s.player(p).trash.size(), trash + 1);
    expect_ok(s);
}

// "You may pay [1] as an additional cost to play me. When you play me, if you paid the additional cost, banish a
// card from any trash to give a unit [Assault 2] this turn."
TEST(RiftboundZed, GustMonk) {
    GameState s = zed_game();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId ally = put(s, p, real("Kinkou Lifeblade"), Zone::Base);
    const ObjId junk = put(s, o, real("Punching Poro"), Zone::Trash);  // the only trash card
    fund(s, p, 3);
    play(s, in_hand(s, p, real("Gust Monk")));
    optional_cost(s, true);
    while (s.decision.purpose != Purpose::Target) apply(s, legal_actions(s).front());
    pick(s, ally);
    settle(s);  // the only card to banish
    EXPECT_EQ(s.obj(junk).zone, Zone::Banishment);
    EXPECT_EQ(traits(s, ally).keywords.get(Keyword::Assault), 2);
    expect_ok(s);
}

// "When I attack, ready another friendly unit."
TEST(RiftboundZed, TwilightReveler) {
    GameState s = zed_game();
    const PlayerId p = s.first_player, o = opponent(p);
    at_field(s, o, real("Punching Poro"), 0);
    const ObjId tired = put(s, p, real("Kinkou Lifeblade"), Zone::Base);
    s.obj(tired).exhausted = true;
    const ObjId reveler = ready_in_base(s, p, real("Twilight Reveler"));
    move_units(s, 0, {reveler});
    settle(s);  // the only other friendly unit
    EXPECT_FALSE(s.obj(tired).exhausted);
    expect_ok(s);
}

// "When an opponent plays a gear, you may banish me to banish it."
TEST(RiftboundZed, RavenbloomPrefect) {
    GameState s = zed_game();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId prefect = put(s, o, real("Ravenbloom Prefect"), Zone::Base);
    const ObjId gear = put(s, p, real("Serrated Dirk"), Zone::Hand);
    fund(s, p, 1);
    play(s, gear);
    while (s.decision.purpose != Purpose::May) apply(s, legal_actions(s).front());
    EXPECT_EQ(s.decision.player, o);
    apply(s, legal_actions(s).front());  // yes
    settle(s);
    EXPECT_EQ(s.obj(prefect).zone, Zone::Banishment);
    EXPECT_EQ(s.obj(gear).zone, Zone::Banishment);
    expect_ok(s);
}

// Legend: "When you banish a card you own, empower me. [Action][>] Disempower me, [E]: Discard 1, then draw 1."
TEST(RiftboundZed, ZedMasterOfShadows) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    const ObjId legend = s.player(p).legend.front();
    s.obj(legend).card = real("Zed, Master of Shadows");
    EXPECT_FALSE(offered(s, {ActionKind::Activate, legend, 1}));  // not Empowered
    const ObjId own = at_field(s, p, real("Punching Poro"), 0);
    fund(s, p, 3, {domain::chaos});
    play(s, in_hand(s, p, real("Wind and Ghosts")));
    settle(s);  // banishes p's own 2 Might unit
    ASSERT_EQ(s.obj(own).zone, Zone::Banishment);
    EXPECT_TRUE(s.obj(legend).empowered);
    const auto hand = s.player(p).hand.size(), trash = s.player(p).trash.size();
    apply(s, {ActionKind::Activate, legend, 1});
    settle(s);
    EXPECT_FALSE(s.obj(legend).empowered);
    EXPECT_TRUE(s.obj(legend).exhausted);
    EXPECT_EQ(s.player(p).hand.size(), hand);
    EXPECT_EQ(s.player(p).trash.size(), trash + 1);
    next_turn_of(s, p);
    EXPECT_FALSE(s.obj(legend).exhausted);  // 315.1.b: legends ready too
    expect_ok(s);
}

// "When you conquer here, discard 1, then draw 1."
TEST(RiftboundZed, ZaunWarrens) {
    GameState s = zed_game();
    const PlayerId p = s.first_player, o = opponent(p);
    set_field(s, 0, real("Zaun Warrens"));
    at_field(s, o, real("Punching Poro"), 0);
    move_units(s, 0, {ready_in_base(s, p, real("Kinkou Lifeblade"))});
    const auto hand = s.player(p).hand.size(), trash = s.player(p).trash.size();
    both_pass(s);
    settle(s);
    ASSERT_EQ(s.battlefields[0].controller, p);
    EXPECT_EQ(s.player(p).hand.size(), hand);
    EXPECT_EQ(s.player(p).trash.size(), trash + 1);
    expect_ok(s);
}

// "When you hold here, [Burn 3]."
TEST(RiftboundZed, ShadowTemple) {
    GameState s = zed_game();
    const PlayerId p = s.first_player;
    set_field(s, 0, real("Shadow Temple"));
    at_field(s, p, real("Kinkou Lifeblade"), 0);
    next_turn_of(s, opponent(p));
    const auto trash = s.player(p).trash.size();
    next_turn_of(s, p);  // p's Beginning Phase: holds
    EXPECT_EQ(s.player(p).trash.size(), trash + 3);
    expect_ok(s);
}

// "When combat starts here, the attacker and defender each [Add] [1]."
TEST(RiftboundZed, ThresholdOfTheGray) {
    GameState s = zed_game();
    const PlayerId p = s.first_player, o = opponent(p);
    set_field(s, 0, real("Threshold of the Gray"));
    at_field(s, o, real("Punching Poro"), 0);
    s.player(p).pool = {};
    move_units(s, 0, {ready_in_base(s, p, real("Kinkou Lifeblade"))});
    settle(s);
    ASSERT_TRUE(s.showdown);
    EXPECT_EQ(s.player(p).pool.energy, 1);
    EXPECT_EQ(s.player(o).pool.energy, 1);
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
