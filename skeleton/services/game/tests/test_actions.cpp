// Game actions (408–446) the decks use, buffs and Mighty (701–711), and the cost kinds they bring (Burn, discard a
// spell, banish me). Actions with tests elsewhere: draw and burn out (test_turn), kill and deal (test_abilities,
// test_chain), move, recall and stun (test_combat), channel (test_turn), discard (test_abilities), recycle
// (test_state, test_pay), predict (Vision in test_keywords), attach/detach and create (test_keywords).
#include <gtest/gtest.h>

#include "riftbound/actions.hpp"
#include "riftbound/engine.hpp"
#include "riftbound/random_game.hpp"
#include "riftbound/pay.hpp"
#include "riftbound/rules.hpp"
#include "test_play.hpp"

using namespace buddy::riftbound;
using namespace buddy::riftbound::test;

namespace {

// Shadow Order Disciple: "When I move, you may [Burn 1] to give me +1 Might this turn."
struct Disciple : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.may = true;
        a.cost.burn = 1;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Moved && e.subject.id == self) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        if (const ObjId me = r.self(); me != no_obj) give_might(r.s, me, 1, r.source(), Duration::Turn);
        return Status::Done;
    }
} const disciple;

// Mel, Defiant Soul's Empower: "[Empower] — Discard a spell".
struct Mel : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.discard = 1;
        a.cost.discard_spell = true;
        a.builtin = Builtin::Empower;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
} const mel;

// Ravenbloom Prefect: "When an opponent plays a gear, you may banish me to banish it."
struct Ravenbloom : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.may = true;
        a.cost.banish_self = true;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Played && e.player != s.obj(self).controller &&
            s.obj(e.subject.id).card->type == CardType::Gear) {
            out.push_back(0);
        }
    }
    Status resolve(Resolution& r) const override {
        const ObjRef gear = r.item().event.subject;
        if (r.s.alive(gear)) banish(r.s, gear.id, by(r));
        return Status::Done;
    }
} const ravenbloom;

// Loyal Poro's Deathknell, drawing: shows banishing isn't dying.
struct Knell : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Die && e.subject.id == self) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        draw(r.s, r.controller(), 1);
        return Status::Done;
    }
} const knell;

const Card* const kUnit = make("Unit", CardType::Unit, 0, 2, plain_rules);
const Card* const kFour = make("Four", CardType::Unit, 0, 4, plain_rules);
const Card* const kFive = make("Five", CardType::Unit, 0, 5, plain_rules);
const Card* const kSpell = make("Spell", CardType::Spell, 0, 0, plain_rules);
const Card* const kGear = make("Gear", CardType::Gear, 0, 0, plain_rules);
const Card* const kDisciple = make("Disciple", CardType::Unit, 0, 2, disciple);
const Card* const kMel = make("Mel", CardType::Unit, 0, 4, mel);
const Card* const kRavenbloom = make("Ravenbloom", CardType::Unit, 0, 3, ravenbloom);
const Card* const kKnell = make("Knell", CardType::Unit, 0, 3, knell);

const Deck& deck() {
    static const Deck d = deck_of("G", {kUnit, kFour, kFive, kSpell, kGear, kDisciple, kMel, kRavenbloom, kKnell});
    return d;
}

GameState started() { return test::started(deck()); }

} // namespace

// ---- Game actions ----

TEST(RiftboundActions, ExhaustAndReady) {  // 415.1.c, 416.1.c
    GameState s = started();
    const ObjId u = put(s, s.first_player, kUnit, Zone::Base);
    exhaust(s, u);
    exhaust(s, u);  // nothing more happens
    EXPECT_TRUE(s.obj(u).exhausted);
    ready(s, u);
    EXPECT_FALSE(s.obj(u).exhausted);
}

TEST(RiftboundActions, RecycleToTheBottom) {  // 417.1.a, 417.1.c
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId u = put(s, p, kUnit, Zone::Trash);
    recycle(s, {u});
    EXPECT_EQ(s.obj(u).zone, Zone::MainDeck);
    EXPECT_EQ(s.player(p).main_deck.front(), u);
}

TEST(RiftboundActions, DealThenHeal) {  // 418.1.b, 419
    GameState s = started();
    const ObjId u = put(s, s.first_player, kFour, Zone::Base);
    EXPECT_EQ(deal(s, u, 2, {}), 2);
    EXPECT_EQ(s.obj(u).damage, 2);
    heal(s, u);
    EXPECT_EQ(s.obj(u).damage, 0);
}

TEST(RiftboundActions, BuffOnlyOnce) {  // 427.1.b, 427.1.b.1, 702.3, 703, 705
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId u = put(s, p, kUnit, Zone::Base);
    EXPECT_TRUE(buff(s, u));
    EXPECT_FALSE(buff(s, u));
    EXPECT_EQ(might(s, u), 3);
    return_to_hand(s, u);  // leaves play: the buff goes
    EXPECT_EQ(s.obj(u).buffs, 0);
}

TEST(RiftboundActions, BanishIsNoDeath) {  // 428.1, 428.2, 428.2.a
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId u = put(s, p, kKnell, Zone::Base);
    const auto gen = s.obj(u).gen;
    banish(s, u, {{}, p});
    EXPECT_EQ(s.obj(u).zone, Zone::Banishment);
    EXPECT_EQ(s.player(p).banishment.back(), u);
    EXPECT_GT(s.obj(u).gen, gen);
    EXPECT_TRUE(s.triggered.empty());  // no Deathknell
    s.cleanup_due = false;  // no engine step ran
    expect_ok(s);
}

TEST(RiftboundActions, ReturnToHandIsANewObject) {  // 124
    GameState s = started();
    const PlayerId o = opponent(s.first_player);
    const ObjId u = put(s, o, kUnit, Zone::Base);
    const ObjRef before = s.obj(u).ref(u);
    return_to_hand(s, u);
    EXPECT_EQ(s.obj(u).zone, Zone::Hand);
    EXPECT_EQ(s.player(o).hand.back(), u);
    EXPECT_FALSE(s.alive(before));
}

TEST(RiftboundActions, AddToThePool) {  // 430.1, 430.5
    GameState s = started();
    const PlayerId p = s.first_player;
    add(s, p, 2, 1);
    EXPECT_EQ(s.player(p).pool.energy, 2);
    EXPECT_EQ(s.player(p).pool.universal, 1);
    Cost c;
    c.energy = 2;
    c.any_power = 1;
    pay(s, p, c, {});  // all from the pool, no rune touched
    EXPECT_EQ(s.player(p).pool, Pool{});
    for (const ObjId r : runes_of(s, p)) EXPECT_FALSE(s.obj(r).exhausted);
}

TEST(RiftboundActions, PayEnergyAsAnInstruction) {  // 445.2
    GameState s = started();
    const PlayerId p = s.first_player;
    EXPECT_FALSE(pay_energy(s, p, 3));  // 2 runes
    EXPECT_TRUE(pay_energy(s, p, 2));
    for (const ObjId r : runes_of(s, p)) EXPECT_TRUE(s.obj(r).exhausted);
}

TEST(RiftboundActions, BurnTopCardsBurnOutMidway) {  // 441.1, 441.4, 432
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    auto& deck = s.player(p).main_deck;
    while (deck.size() > 1) move_to(s, deck.front(), Zone::Removed, p);  // one card left, empty trash
    const ObjId last = deck.back();
    burn(s, p, 2);  // burns it, burns out (it comes back), burns it again
    EXPECT_EQ(s.player(o).points, 1);
    EXPECT_EQ(s.obj(last).zone, Zone::Trash);
    EXPECT_TRUE(deck.empty());
}

TEST(RiftboundActions, EmpowerOnceDisempowerOnlyIfEmpowered) {  // 442.1.c, 442.2.a, 443.1.a.1
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId u = put(s, p, kUnit, Zone::Base);
    disempower(s, u);
    EXPECT_FALSE(s.obj(u).empowered);
    empower(s, u);
    empower(s, u);
    EXPECT_TRUE(s.obj(u).empowered);
    disempower(s, u);
    EXPECT_FALSE(s.obj(u).empowered);
}

// ---- Mighty ----

TEST(RiftboundMighty, CurrentOnTheBoardPrintedElsewhere) {  // 708, 710, 711
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId four = put(s, p, kFour, Zone::Base);
    EXPECT_FALSE(mighty(s, four));
    give_might(s, four, 1, {}, Duration::Turn);
    EXPECT_TRUE(mighty(s, four));
    const ObjId five = put(s, p, kFive, Zone::Hand);
    EXPECT_TRUE(mighty(s, five));  // printed 5
    const ObjId weak = put(s, p, kFive, Zone::Base);
    give_might(s, weak, -1, {}, Duration::Turn);
    EXPECT_FALSE(mighty(s, weak));
}

// ---- Costs ----

TEST(RiftboundCosts, BurnAsATriggerCost) {  // 383.3.b, 441.3.a
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId d = put(s, p, kDisciple, Zone::Battlefield, 0);
    s.battlefields[0].controller = p;
    const auto trash = s.player(p).trash.size();
    apply(s, {ActionKind::Move, no_obj, to_base});  // Disciple is the only unit that can go
    ASSERT_EQ(s.decision.purpose, Purpose::May);
    pick(s, d);
    EXPECT_EQ(s.player(p).trash.size(), trash + 1);  // burned while finalizing
    both_pass(s);
    EXPECT_EQ(might(s, d), 3);
    expect_ok(s);
}

TEST(RiftboundCosts, DiscardASpell) {  // Mel's Empower cost, 357.2
    GameState s = started();
    const PlayerId p = s.first_player;
    for (const ObjId id : std::vector<ObjId>(s.player(p).hand)) move_to(s, id, Zone::Trash, p);
    const ObjId m = put(s, p, kMel, Zone::Base);
    put(s, p, kUnit, Zone::Hand);
    const Action empower_it{ActionKind::Activate, m, 0};
    EXPECT_FALSE(offered(s, empower_it));  // a unit doesn't pay
    const ObjId spell = put(s, p, kSpell, Zone::Hand);
    ASSERT_TRUE(offered(s, empower_it));
    apply(s, empower_it);  // the only spell: no decision
    EXPECT_EQ(s.obj(spell).zone, Zone::Trash);
    both_pass(s);
    EXPECT_TRUE(s.obj(m).empowered);
    expect_ok(s);
}

TEST(RiftboundCosts, BanishMeToBanishIt) {  // Ravenbloom Prefect: a trigger cost (383.3.b), 428
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId raven = put(s, o, kRavenbloom, Zone::Base);
    const ObjId gear = put(s, p, kGear, Zone::Hand);
    play(s, gear);
    ASSERT_EQ(s.decision.purpose, Purpose::May);
    EXPECT_EQ(s.decision.player, o);
    pick(s, raven);
    EXPECT_EQ(s.obj(raven).zone, Zone::Banishment);  // paid while finalizing
    both_pass(s);
    EXPECT_EQ(s.obj(gear).zone, Zone::Banishment);
    expect_ok(s);
}

// ---- Random games with the cost cards ----

TEST(RiftboundRandom, CostDecks) {  // Burn, discard a spell and banish-me costs under every check
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        const RandomGame g = play_random(deck(), deck(), seed, 100000, false);
        ASSERT_TRUE(g.problem.empty()) << "seed " << seed << ": " << g.problem;
        EXPECT_TRUE(g.winner.has_value());
    }
}
