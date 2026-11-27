// The decks' keywords (805–829) beyond the combat ones in test_combat.cpp, attachment and Equipment (716–725, 818)
// and tokens (179–187, 440). Accelerate and Deathknell: test_abilities.cpp. Test cards are shaped like the deck cards
// named in their comments.
#include <gtest/gtest.h>

#include "riftbound/actions.hpp"
#include "riftbound/engine.hpp"
#include "riftbound/random_game.hpp"
#include "riftbound/pay.hpp"
#include "riftbound/rules.hpp"
#include "riftbound/tokens.hpp"
#include "test_play.hpp"

using namespace buddy::riftbound;
using namespace buddy::riftbound::test;

namespace {

// Hidden Blade's shape without the draw: "Deal 3 to a unit."
struct Bolt : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) deal(r.s, t, 3, by(r));
        return Status::Done;
    }
} const bolt;

// Divining Shells' [Vision], nothing else.
const Keyworded vision(Keyword::Vision);
const Keyworded deflect(Keyword::Deflect);
const Keyworded ambush(Keyword::Ambush);

// Upstage Comedy: "[Repeat] [2] Ready a unit."
struct Comedy : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr Cost kRepeat[] = {{2}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    std::span<const Cost> repeat_costs() const override { return kRepeat; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) ready(r.s, t);
        return Status::Done;
    }
} const comedy;

// Punching Poro: "[Empower] — Discard 1. [Empowered][>] I have +1 Might."
struct Puncher : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.discard = 1;
        a.builtin = Builtin::Empower;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void passive(const GameState& s, ObjId self, ObjId subject, const Traits&, std::vector<Mod>& out) const override {
        if (subject == self && s.obj(self).empowered) out.push_back({ModKind::Might, {}, 1});
    }
} const puncher;

// Twilight Step's Flow on a simpler effect: "Draw 1. [Flow] [1][A]"
struct Step : CardRules {
    std::optional<Cost> flow_cost() const override {
        Cost c;
        c.energy = 1;
        c.any_power = 1;
        return c;
    }
    Status resolve(Resolution& r) const override {
        draw(r.s, r.controller(), 1);
        return Status::Done;
    }
} const step;

// Zed, Master of Shadows' trigger: "When you banish a card you own, draw 1."
struct Banisher : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        const PlayerId me = s.obj(self).controller;
        if (e.kind == EventKind::Banished && e.player == me && s.obj(e.subject.id).owner == me) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        draw(r.s, r.controller(), 1);
        return Status::Done;
    }
} const banisher;

// Serrated Dirk: "[Equip] [Fury]", with a Might Bonus of +2 (not in Riftcodex).
struct Dirk : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.power = 1;
        a.cost.domains = domain::fury;
        a.builtin = Builtin::Equip;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    int might_bonus() const override { return 2; }
} const dirk;

// Hextech Gauntlets: "[Equip] [4][A]. This ability's Energy cost is reduced by the Might of the unit you choose."
// ([3] on the card; [4] here so the 2 runes of turn 1 can't pay for a 1 Might unit.)
struct Gauntlets : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.energy = 4;
        a.cost.any_power = 1;
        a.builtin = Builtin::Equip;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void cost_mods(const GameState& s, ObjId self, const ChainItem& item, std::vector<CostMod>& out) const override {
        if (item.kind != ItemKind::Activated || item.source.id != self || item.targets.empty()) return;
        out.push_back({CostMod::Discount, std::max(0, might(s, item.targets.front().ref.id)), 0, 0});
    }
} const gauntlets;

// Carrion Dredger: "[Deathknell][>] Play a 1 Might Bird unit token with [Deflect] to your base."
struct Dredger : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Keywords keywords() const override {
        Keywords k;
        k.grant(Keyword::Deathknell);
        return k;
    }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Die && e.subject.id == self) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        play_token(r.s, tokens::bird(), r.controller(), played::to_base);
        return Status::Done;
    }
} const dredger;

// Death Mark's token part: "Play a 0 Might Shadow Clone unit token."
struct Mark : CardRules {
    Status resolve(Resolution& r) const override {
        play_token(r.s, tokens::shadow_clone(), r.controller());
        return Status::Done;
    }
} const mark;

// Yeti Brawler's reward as a spell: "Play two Gold gear tokens exhausted."
struct Riches : CardRules {
    Status resolve(Resolution& r) const override {
        for (int i = 0; i < 2; ++i) play_token(r.s, tokens::gold(), r.controller(), played::exhausted);
        return Status::Done;
    }
} const riches;

const Card* const kUnit = make("Unit", CardType::Unit, 0, 2, plain_rules);
const Card* const kBig = make("Big", CardType::Unit, 0, 3, plain_rules);
const Card* const kSmall = make("Small", CardType::Unit, 0, 1, plain_rules);
const Card* const kBolt = make("Bolt", CardType::Spell, 0, 0, bolt);
const Card* const kQuiet = make("Quiet", CardType::Spell, 0, 0, plain_rules);
const Card* const kSeer = make("Seer", CardType::Gear, 0, 0, vision);
const Card* const kWarded = make("Warded", CardType::Unit, 0, 3, deflect);
const Card* const kComedy = make("Comedy", CardType::Spell, 0, 0, comedy);
const Card* const kAmbusher = make("Ambusher", CardType::Unit, 0, 1, ambush);
const Card* const kPuncher = make("Puncher", CardType::Unit, 0, 2, puncher);
const Card* const kStep = make("Step", CardType::Spell, 2, 0, step);
const Card* const kBanisher = make("Banisher", CardType::Unit, 0, 1, banisher);
const Card* const kDirk = make("Dirk", CardType::Gear, 0, 0, dirk);
const Card* const kGauntlets = make("Gauntlets", CardType::Gear, 0, 0, gauntlets);
const Card* const kDredger = make("Dredger", CardType::Unit, 0, 1, dredger);
const Card* const kMark = make("Mark", CardType::Spell, 0, 0, mark);
const Card* const kRiches = make("Riches", CardType::Spell, 0, 0, riches);

const Deck& deck() {
    static const Deck d = deck_of("K", {kUnit, kBig, kSmall, kBolt, kQuiet, kSeer, kWarded, kComedy, kAmbusher,
                                        kPuncher, kStep, kBanisher, kDirk, kGauntlets, kDredger, kMark, kRiches});
    return d;
}

GameState started() { return test::started(deck()); }

// p's runes leave the base: nothing to pay with.
void no_runes(GameState& s, PlayerId p) {
    for (const ObjId id : runes_of(s, p)) move_to(s, id, Zone::RuneDeck, p);
    s.cleanup_due = false;
}

} // namespace

// ---- Deflect ----

TEST(RiftboundKeywords, DeflectCostsAnyPowerToChoose) {  // 809.1.c, 809.1.c.1, 809.1.d
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId warded = put(s, o, kWarded, Zone::Base);
    const auto runes = runes_of(s, p).size();
    play(s, put(s, p, kBolt, Zone::Hand));  // the only unit: chosen without a decision
    ASSERT_EQ(s.chain.size(), 1u);
    EXPECT_EQ(s.chain[0].cost.any_power, 1);
    EXPECT_EQ(runes_of(s, p).size(), runes - 1);  // a Fury rune recycled for [A]
    both_pass(s);
    EXPECT_EQ(s.obj(warded).zone, Zone::Trash);
    expect_ok(s);
}

TEST(RiftboundKeywords, DeflectUnaffordableCantBeChosen) {  // 809.1.d, 355.16; own units: no cost
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    no_runes(s, p);
    put(s, o, kWarded, Zone::Base);
    const ObjId bolt_id = put(s, p, kBolt, Zone::Hand);
    EXPECT_FALSE(offered(s, {ActionKind::Play, bolt_id}));
    const ObjId mine = put(s, p, kWarded, Zone::Base);  // Deflect works against opponents only
    play(s, bolt_id);  // the enemy one isn't offered: no decision
    EXPECT_EQ(s.chain[0].targets.front().ref.id, mine);
    EXPECT_EQ(s.chain[0].cost.any_power, 0);
    expect_ok(s);
}

TEST(RiftboundKeywords, DeflectValuesAddUp) {  // 809.2
    GameState s = started();
    const PlayerId o = opponent(s.first_player);
    const ObjId warded = put(s, o, kWarded, Zone::Base);
    give(s, {.subject = s.obj(warded).ref(warded), .source = {}, .kind = ModKind::Grant, .keyword = Keyword::Deflect,
             .value = 1});
    EXPECT_EQ(traits(s, warded).keywords.get(Keyword::Deflect), 2);
}

// ---- Vision ----

TEST(RiftboundKeywords, VisionRecyclesTheTopCard) {  // 817.1, 437.1
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId top = s.player(p).main_deck.back();
    play(s, put(s, p, kSeer, Zone::Hand));  // gear: on the board at once, its Vision triggers
    ASSERT_EQ(s.chain.size(), 1u);
    EXPECT_EQ(s.chain[0].ability, ability_vision);
    both_pass(s);
    ASSERT_EQ(s.decision.kind, DecisionKind::Choose);
    EXPECT_EQ(s.decision.player, p);
    EXPECT_EQ(legal_actions(s).front(), (Action{ActionKind::Pick, top}));  // p sees the top card
    pick(s, top);
    EXPECT_EQ(s.player(p).main_deck.front(), top);  // recycled to the bottom
    expect_ok(s);
}

TEST(RiftboundKeywords, VisionMayKeepTheTopCard) {  // 437.1: recycling is optional
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId top = s.player(p).main_deck.back();
    play(s, put(s, p, kSeer, Zone::Hand));
    both_pass(s);
    done(s);
    EXPECT_EQ(s.player(p).main_deck.back(), top);
}

// ---- Repeat ----

TEST(RiftboundKeywords, RepeatRunsAgainWithItsOwnTarget) {  // 820.1.d, 820.2, 820.2.a, 355.1.a
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId a = put(s, p, kUnit, Zone::Base), b = put(s, p, kBig, Zone::Base);
    s.obj(a).exhausted = s.obj(b).exhausted = true;
    const ObjId c = put(s, p, kComedy, Zone::Hand);
    play(s, c);
    ASSERT_EQ(s.decision.purpose, Purpose::OptionalCost);  // before targets (355.1.a)
    pick(s, c);  // pays [2]: both runes
    ASSERT_EQ(s.decision.purpose, Purpose::Target);
    pick(s, a);
    ASSERT_EQ(s.decision.purpose, Purpose::Target);  // the repeat's own choice
    pick(s, b);
    ASSERT_EQ(s.chain[0].targets.size(), 2u);
    both_pass(s);
    EXPECT_FALSE(s.obj(a).exhausted);
    EXPECT_FALSE(s.obj(b).exhausted);
    expect_ok(s);
}

TEST(RiftboundKeywords, RepeatNotPaidRunsOnce) {
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId a = put(s, p, kUnit, Zone::Base);
    s.obj(a).exhausted = true;
    play(s, put(s, p, kComedy, Zone::Hand));
    done(s);  // decline: one slot, its only candidate
    EXPECT_EQ(s.chain[0].targets.size(), 1u);
    both_pass(s);
    EXPECT_FALSE(s.obj(a).exhausted);
}

// ---- Ambush ----

TEST(RiftboundKeywords, AmbushIsAReactionToWhereYouHaveUnits) {  // 822.1.b, 822.1.c
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId amb = put(s, o, kAmbusher, Zone::Hand);
    play(s, put(s, p, kQuiet, Zone::Hand));
    pass(s);  // p may react to their own spell first (338.1.a.5)
    ASSERT_EQ(s.decision.player, o);
    EXPECT_FALSE(offered(s, {ActionKind::Play, amb}));  // no units at a battlefield

    put(s, o, kUnit, Zone::Battlefield, 1);
    s.battlefields[1].controller = o;
    ASSERT_TRUE(offered(s, {ActionKind::Play, amb}));
    play(s, amb);  // only battlefield 1: no base, no decision
    EXPECT_EQ(s.obj(amb).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(amb).where, 1);
    expect_ok(s);
}

TEST(RiftboundKeywords, AmbushAddsLocationsToANormalPlay) {  // 822.1.c, 355.2.b
    GameState s = started();
    const PlayerId p = s.first_player;
    put(s, p, kUnit, Zone::Battlefield, 0);  // p has a unit there but doesn't control it
    s.cleanup_due = false;
    play(s, put(s, p, kAmbusher, Zone::Hand));
    ASSERT_EQ(s.decision.purpose, Purpose::Location);
    const auto options = legal_actions(s);
    ASSERT_EQ(options.size(), 2u);  // battlefield 0, base
    EXPECT_EQ(options[0].n, 0);
    EXPECT_EQ(options[1].n, to_base);
}

// ---- Empower and Empowered ----

TEST(RiftboundKeywords, EmpowerOnceThenEmpowered) {  // 827.1.c.1, 827.2, 828.1.b.1
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId poro = put(s, p, kPuncher, Zone::Base);
    const Action empower_it{ActionKind::Activate, poro, 0};
    ASSERT_TRUE(offered(s, empower_it));
    const auto hand = s.player(p).hand.size();
    apply(s, empower_it);
    if (s.decision.purpose == Purpose::Discard) apply(s, legal_actions(s).front());
    EXPECT_EQ(s.player(p).hand.size(), hand - 1);
    both_pass(s);
    EXPECT_TRUE(s.obj(poro).empowered);
    EXPECT_EQ(might(s, poro), 3);
    EXPECT_FALSE(offered(s, empower_it));  // only if not Empowered
    expect_ok(s);
}

// ---- Flow ----

TEST(RiftboundKeywords, FlowFromTheTrashThenBanished) {  // 829.1.b, 829.1.b.1, 829.1.c.1, 428
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId step_id = put(s, p, kStep, Zone::Trash);
    const ObjId quiet = put(s, p, kQuiet, Zone::Trash);
    put(s, p, kBanisher, Zone::Base);
    EXPECT_FALSE(offered(s, {ActionKind::Play, quiet}));  // no Flow
    ASSERT_TRUE(offered(s, {ActionKind::Play, step_id}));
    const auto hand = s.player(p).hand.size();
    play(s, step_id);
    EXPECT_EQ(s.chain[0].cost.energy, 1);  // [1][A] instead of its printed [2]
    EXPECT_EQ(s.chain[0].cost.any_power, 1);
    both_pass(s);
    EXPECT_EQ(s.obj(step_id).zone, Zone::Banishment);
    settle(s);  // Banisher: p banished a card p owns
    EXPECT_EQ(s.player(p).hand.size(), hand + 2);
    expect_ok(s);
}

// ---- Equipment and attachment ----

TEST(RiftboundAttach, EquipAttachesAndAddsTheMightBonus) {  // 818.1.c.2, 435.4, 137.3, 718.2, 721.2
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId gear = put(s, p, kDirk, Zone::Base);
    const ObjId unit = put(s, p, kUnit, Zone::Base);
    const Action equip{ActionKind::Activate, gear, 0};
    ASSERT_TRUE(offered(s, equip));
    apply(s, equip);  // the only friendly unit: chosen without a decision
    both_pass(s);
    EXPECT_EQ(s.obj(gear).attached, unit);
    EXPECT_EQ(might(s, unit), 4);
    EXPECT_FALSE(offered(s, equip));  // attached: its rules text is inactive
    expect_ok(s);
}

TEST(RiftboundAttach, GearGoesAlongAndDetachesWhenTheUnitLeaves) {  // 719.3.a, 719.5, 436.4.a–b, 323.6
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId gear = put(s, p, kDirk, Zone::Base);
    const ObjId unit = put(s, p, kUnit, Zone::Base);
    attach(s, gear, unit);
    move(s, {unit}, 0, {{}, p});  // not a move for the gear, it just goes along
    EXPECT_EQ(s.obj(gear).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(gear).where, 0);
    EXPECT_EQ(s.obj(gear).attached, unit);
    kill(s, unit, {{}, p});
    EXPECT_EQ(s.obj(gear).attached, no_obj);
    EXPECT_EQ(s.obj(gear).zone, Zone::Battlefield);  // detached where the unit was
    play(s, put(s, p, kQuiet, Zone::Hand));  // any resolution: the cleanups after it recall the gear
    both_pass(s);
    EXPECT_EQ(s.obj(gear).zone, Zone::Base);
    expect_ok(s);
}

TEST(RiftboundAttach, DetachAndReattach) {  // 435.1.f, 435.1.g, 435.1.h, 436.1.a.1
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId gear = put(s, p, kDirk, Zone::Base);
    const ObjId a = put(s, p, kUnit, Zone::Base), b = put(s, p, kBig, Zone::Base);
    attach(s, gear, a);
    attach(s, gear, b);  // detaches from a first
    EXPECT_EQ(s.obj(gear).attached, b);
    EXPECT_EQ(might(s, a), 2);
    attach(s, gear, put(s, p, kSeer, Zone::Base));  // not a unit: nothing
    EXPECT_EQ(s.obj(gear).attached, b);
    detach(s, gear);
    detach(s, gear);  // not attached: nothing
    EXPECT_EQ(s.obj(gear).attached, no_obj);
    EXPECT_EQ(might(s, b), 3);
}

TEST(RiftboundAttach, EquipCostDependsOnTheChosenUnit) {  // 818.1.c.4, 355.16
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId gear = put(s, p, kGauntlets, Zone::Base);
    const ObjId small = put(s, p, kSmall, Zone::Base);
    const Action equip{ActionKind::Activate, gear, 0};
    EXPECT_FALSE(offered(s, equip));  // [3] + [A] for the 1 Might unit: 2 runes can't
    const ObjId big = put(s, p, kBig, Zone::Base);
    ASSERT_TRUE(offered(s, equip));
    apply(s, equip);  // only the big one keeps it payable: no decision
    EXPECT_EQ(s.chain.back().targets.front().ref.id, big);
    EXPECT_EQ(s.chain.back().cost.energy, 1);
    both_pass(s);
    EXPECT_EQ(s.obj(gear).attached, big);
    EXPECT_EQ(s.obj(small).zone, Zone::Base);
    expect_ok(s);
}

// ---- Tokens ----

TEST(RiftboundTokens, DeathknellPlaysABirdToTheBase) {  // 808.1.c, 182, 183, 184.2, 185.2.d, 440.2.c
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId dredger_id = put(s, o, kDredger, Zone::Base);
    const auto objects = s.objects.size();
    play(s, put(s, p, kBolt, Zone::Hand));
    both_pass(s);  // Bolt; the cleanup kills Dredger, its Deathknell goes on the chain
    EXPECT_EQ(s.obj(dredger_id).zone, Zone::Trash);
    both_pass(s);
    ASSERT_EQ(s.objects.size(), objects + 1);
    const ObjId bird = static_cast<ObjId>(objects);
    const Object& b = s.obj(bird);
    EXPECT_TRUE(b.card->token);
    EXPECT_EQ(b.zone, Zone::Base);
    EXPECT_EQ(b.owner, o);
    EXPECT_EQ(b.controller, o);
    EXPECT_TRUE(b.exhausted);  // units enter exhausted
    EXPECT_EQ(might(s, bird), 1);
    EXPECT_TRUE(has(s, bird, Keyword::Deflect));
    expect_ok(s);
}

TEST(RiftboundTokens, ATokenOffTheBoardStopsExisting) {  // 186.1, 185.1
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId bird = create(s, &tokens::bird(), p, Zone::Base);
    return_to_hand(s, bird);
    EXPECT_EQ(s.obj(bird).zone, Zone::Removed);
    EXPECT_TRUE(s.player(p).hand.empty() || s.player(p).hand.back() != bird);
    const ObjId clone = create(s, &tokens::shadow_clone(), p, Zone::Base);
    banish(s, clone, {{}, p});
    EXPECT_EQ(s.obj(clone).zone, Zone::Removed);
    s.cleanup_due = false;  // no engine step ran
    expect_ok(s);
}

TEST(RiftboundTokens, PlayedTokenChoosesItsLocation) {  // 440.2.c, 185.2.a, 355.2.a
    GameState s = started();
    const PlayerId p = s.first_player;
    put(s, p, kUnit, Zone::Battlefield, 0);
    s.battlefields[0].controller = p;
    const auto objects = s.objects.size();
    play(s, put(s, p, kMark, Zone::Hand));
    both_pass(s);
    ASSERT_EQ(s.decision.purpose, Purpose::Location);  // like any unit played
    pick(s, s.battlefields[0].card);
    const ObjId clone = static_cast<ObjId>(objects);
    EXPECT_EQ(s.obj(clone).zone, Zone::Battlefield);
    EXPECT_EQ(might(s, clone), 0);
    expect_ok(s);
}

TEST(RiftboundTokens, ShadowCloneBanishesForAssault) {  // Shadow Clone's attack trigger, 428, 807
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId clone = create(s, &tokens::shadow_clone(), p, Zone::Base);
    const ObjId dead = put(s, p, kUnit, Zone::Trash);
    put(s, o, kBig, Zone::Battlefield, 0);
    s.battlefields[0].controller = o;
    s.cleanup_due = false;
    apply(s, {ActionKind::Move, s.battlefields[0].card, 0});  // the clone is the only unit that can go
    ASSERT_TRUE(s.showdown && s.showdown->combat);
    both_pass(s);  // its Attack trigger resolves
    ASSERT_EQ(s.decision.kind, DecisionKind::Choose);
    pick(s, dead);
    EXPECT_EQ(s.obj(dead).zone, Zone::Banishment);
    EXPECT_EQ(might(s, clone), 4);  // Assault 4 while attacking
    expect_ok(s);
}

TEST(RiftboundTokens, GoldEntersExhaustedAddsAnyPowerAndGoes) {  // 184.1, 430.2, 186.1
    GameState s = started();
    const PlayerId p = s.first_player;
    const auto objects = s.objects.size();
    play(s, put(s, p, kRiches, Zone::Hand));
    both_pass(s);
    ASSERT_EQ(s.objects.size(), objects + 2);
    const ObjId gold = static_cast<ObjId>(objects);
    EXPECT_EQ(s.obj(gold).zone, Zone::Base);
    EXPECT_TRUE(s.obj(gold).exhausted);  // gear enters ready unless played exhausted
    const Action use{ActionKind::Activate, gold, 0};
    EXPECT_FALSE(offered(s, use));
    s.obj(gold).exhausted = false;
    ASSERT_TRUE(offered(s, use));
    apply(s, use);  // an Add ability: resolves at once, no priority
    EXPECT_TRUE(s.chain.empty());
    EXPECT_EQ(s.player(p).pool.universal, 1);
    EXPECT_EQ(s.obj(gold).zone, Zone::Removed);  // killed as the cost: a token stops existing
    expect_ok(s);
}

// ---- Random games with every keyword card and token ----

TEST(RiftboundRandom, KeywordDecks) {  // invariants after every action, outside vanilla mode
    int activated = 0, choices = 0;
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        const RandomGame g = play_random(deck(), deck(), seed, 100000, false);
        ASSERT_TRUE(g.problem.empty()) << "seed " << seed << ": " << g.problem;
        EXPECT_TRUE(g.winner.has_value());
        activated += g.activations;
        choices += g.choices;
    }
    EXPECT_GT(activated, 100);  // Equip, Empower, Gold
    EXPECT_GT(choices, 500);
}
