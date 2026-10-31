// Movement (144, 447–460), control and Contested (188–190), showdowns (341–348), combat (461–468) and scoring
// (469–474), with the cleanup steps that stage them (323.2, 323.5, 323.7–323.13).
#include <gtest/gtest.h>

#include <algorithm>

#include "riftbound/actions.hpp"
#include "riftbound/engine.hpp"
#include "riftbound/rules.hpp"
#include "test_cards.hpp"

using namespace buddy::riftbound;

namespace {

const Card* make(std::string name, int might, const CardRules& rules, CardType type = CardType::Unit,
                 Timing timing = Timing::Normal) {
    Card c;
    c.name = std::move(name);
    c.type = type;
    c.domains = domain::fury;
    c.energy = 0;
    if (type == CardType::Unit) c.might = might;
    if (type == CardType::Spell) c.power = 0;
    c.timing = timing;
    c.rules = &rules;
    return test::card(std::move(c));
}

struct Keyworded : CardRules {
    Keywords k;
    explicit Keyworded(Keyword kw, int n = 1) { k.grant(kw, n); }
    Keywords keywords() const override { return k; }
};
const Keyworded ganking(Keyword::Ganking), tank(Keyword::Tank), assault2(Keyword::Assault, 2),
    shield1(Keyword::Shield, 1);

// One triggered ability that draws 1, on events `when` accepts.
template <bool (*when)(const GameState&, const Event&, ObjId)>
struct DrawOn : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (when(s, e, self)) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        draw(r.s, r.controller(), 1);
        return Status::Done;
    }
};

// Traveling Merchant's shape: "When I move, draw 1."
bool i_move(const GameState&, const Event& e, ObjId self) { return e.kind == EventKind::Moved && e.subject.id == self; }
// Twilight Reveler's shape: "When I attack, draw 1."
bool i_attack(const GameState&, const Event& e, ObjId self) { return e.kind == EventKind::Attack && e.subject.id == self; }
// Yeti Brawler's shape: "When I conquer, if you assigned 3 or more excess damage, draw 1."
bool i_conquer(const GameState& s, const Event& e, ObjId self) {
    const Object& o = s.obj(self);
    return e.kind == EventKind::Conquer && e.player == o.controller && o.zone == Zone::Battlefield &&
           s.battlefields[o.where].card == e.subject.id && e.amount >= 3;
}
const DrawOn<i_move> merchant;
const DrawOn<i_attack> reveler;
const DrawOn<i_conquer> yeti;

// Twilight Step's shape for showdowns: "[Action] Move a friendly unit in your base to the showdown's battlefield."
bool friendly_in_base(const GameState& s, const ChainItem& item, const Target& t) {
    const Object& o = s.obj(t.ref.id);
    return o.card->type == CardType::Unit && o.zone == Zone::Base && o.controller == item.controller;
}
struct Charge : CardRules {
    static constexpr TargetSpec kTargets[] = {{friendly_in_base}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj && r.s.showdown) {
            move(r.s, {t}, r.s.showdown->where, {r.source(), r.controller()});
        }
        return Status::Done;
    }
} const charge;

const Card* const kTwo = make("Two", 2, test::plain_rules);
const Card* const kThree = make("Three", 3, test::plain_rules);
const Card* const kFive = make("Five", 5, test::plain_rules);
const Card* const kGanker = make("Ganker", 2, ganking);
const Card* const kTank = make("Tank", 2, tank);
const Card* const kRaider = make("Raider", 1, assault2);
const Card* const kGuard = make("Guard", 2, shield1);
const Card* const kMerchant = make("Merchant", 1, merchant);
const Card* const kReveler = make("Reveler", 1, reveler);
const Card* const kYeti = make("Yeti", 6, yeti);
const Card* const kCharge = make("Charge", 0, charge, CardType::Spell, Timing::Action);
const Card* const kQuick = make("Quick", 0, test::plain_rules, CardType::Spell, Timing::Action);

const Deck& deck() {
    static const Deck d = [] {
        Deck deck = test::ruled(test::simple_deck("C"));
        deck.main.clear();
        for (const Card* c : {kTwo, kThree, kFive, kGanker, kTank, kRaider, kGuard, kMerchant, kReveler, kYeti,
                              kCharge, kQuick}) {
            deck.main.push_back({c, 3});
        }
        return deck;
    }();
    return d;
}

// Both keep: turn 1's main-phase priority for the first player. Nobody controls a battlefield yet.
GameState started() {
    GameState s = new_game(deck(), deck(), {.seed = 7});
    apply(s, {ActionKind::Done});
    apply(s, {ActionKind::Done});
    return s;
}

// A copy of `card` of player p out of the deck (or the hand) into `zone`, ready. Board zones: p controls it.
ObjId put(GameState& s, PlayerId p, const Card* card, Zone zone, std::uint8_t where = 0) {
    for (const bool from_hand : {false, true}) {
        for (ObjId id = 0; id < s.objects.size(); ++id) {
            const Object& o = s.obj(id);
            if (o.card != card || o.owner != p || on_board(o.zone) || o.zone == Zone::Chain || o.zone == zone) continue;
            if ((o.zone == Zone::Hand) != from_hand) continue;
            move_to(s, id, zone, p, where);
            s.cleanup_due = false;
            return id;
        }
    }
    throw std::logic_error("no copy left of " + card->name);
}

// p's unit(s) hold battlefield b for p.
void holds(GameState& s, PlayerId p, std::uint8_t b) { s.battlefields[b].controller = p; }

bool offered(const GameState& s, const Action& a) {
    const auto actions = legal_actions(s);
    return std::find(actions.begin(), actions.end(), a) != actions.end();
}

Action move_action(const GameState& s, std::uint8_t where) {
    return {ActionKind::Move, where == to_base ? no_obj : s.battlefields[where].card, where};
}

// A Standard Move of `ids` to `where`.
void move_units(GameState& s, std::uint8_t where, std::vector<ObjId> ids) {
    apply(s, move_action(s, where));
    std::sort(ids.begin(), ids.end());
    for (const ObjId id : ids) {
        if (s.decision.purpose == Purpose::Move) apply(s, {ActionKind::Pick, id});
    }
    if (s.decision.purpose == Purpose::Move) apply(s, {ActionKind::Done});
}

void play(GameState& s, ObjId id) { apply(s, {ActionKind::Play, id}); }
void pass(GameState& s) { apply(s, {ActionKind::Pass}); }
void pick(GameState& s, ObjId id) { apply(s, {ActionKind::Pick, id}); }
void both_pass(GameState& s) {
    pass(s);
    pass(s);
}

std::vector<Action> picks(std::initializer_list<ObjId> ids) {
    std::vector<Action> out;
    for (const ObjId id : ids) out.push_back({ActionKind::Pick, id});
    return out;
}

void expect_ok(const GameState& s) {
    const auto problems = check_invariants(s);
    EXPECT_TRUE(problems.empty()) << problems.front();
}

} // namespace

// ---- Movement ----

TEST(RiftboundMove, StandardMoveDestinations) {  // 144.1, 144.4, 810
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId home = put(s, p, kTwo, Zone::Base);
    EXPECT_TRUE(offered(s, move_action(s, 0)));
    EXPECT_TRUE(offered(s, move_action(s, 1)));
    EXPECT_FALSE(offered(s, move_action(s, to_base)));
    exhaust(s, home);  // 144.2: the cost
    s.cleanup_due = false;
    EXPECT_FALSE(offered(s, move_action(s, 0)));

    put(s, p, kTwo, Zone::Battlefield, 0);
    holds(s, p, 0);
    EXPECT_TRUE(offered(s, move_action(s, to_base)));
    EXPECT_FALSE(offered(s, move_action(s, 1)));  // battlefield to battlefield needs Ganking
    put(s, p, kGanker, Zone::Battlefield, 0);
    EXPECT_TRUE(offered(s, move_action(s, 1)));
    expect_ok(s);

    play(s, put(s, p, kQuick, Zone::Hand));  // 144.1.b: not in a closed state
    EXPECT_FALSE(offered(s, move_action(s, to_base)));
}

TEST(RiftboundMove, SeveralUnitsMoveAsOne) {  // 144.3, 452, 455
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId a = put(s, p, kTwo, Zone::Base), b = put(s, p, kThree, Zone::Base);
    apply(s, move_action(s, 1));
    ASSERT_EQ(s.decision.purpose, Purpose::Move);
    EXPECT_EQ(s.decision.options, picks({a, b}));
    EXPECT_EQ(s.decision.min, 1);
    pick(s, a);
    pick(s, b);  // all picked: finished without Done
    for (const ObjId id : {a, b}) {
        EXPECT_EQ(s.obj(id).zone, Zone::Battlefield);
        EXPECT_EQ(s.obj(id).where, 1);
        EXPECT_TRUE(s.obj(id).exhausted);
    }
    EXPECT_EQ(s.battlefields[1].contested, p);
    ASSERT_TRUE(s.showdown);  // one showdown for both (323.11)
    EXPECT_EQ(s.showdown->where, 1);
    expect_ok(s);
}

TEST(RiftboundMove, OwnBattlefieldIsNotContested) {  // 452, 190.3.a.1
    GameState s = started();
    const PlayerId p = s.first_player;
    put(s, p, kTwo, Zone::Battlefield, 0);
    holds(s, p, 0);
    move_units(s, 0, {put(s, p, kThree, Zone::Base)});
    EXPECT_FALSE(s.battlefields[0].contested);
    EXPECT_FALSE(s.showdown);
    EXPECT_EQ(s.decision.player, p);
    expect_ok(s);
}

TEST(RiftboundMove, MovedTriggerKeepsFocus) {  // 421 (Moved event), 346.1: a triggered chain doesn't pass focus
    GameState s = started();
    const PlayerId p = s.first_player;
    const auto hand = s.player(p).hand.size();
    move_units(s, 1, {put(s, p, kMerchant, Zone::Base)});
    ASSERT_TRUE(s.showdown);
    ASSERT_EQ(s.chain.size(), 1u);  // the trigger, put on the chain in the showdown
    both_pass(s);
    EXPECT_EQ(s.player(p).hand.size(), hand + 1);
    EXPECT_TRUE(s.chain.empty());
    EXPECT_EQ(s.decision.player, p);
    EXPECT_EQ(s.showdown->focus, p);
    expect_ok(s);
}

TEST(RiftboundMove, RecallIsNoMove) {  // 457, 458.1, 460
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId m = put(s, p, kMerchant, Zone::Battlefield, 0);
    holds(s, p, 0);
    exhaust(s, m);
    recall(s, m);
    EXPECT_EQ(s.obj(m).zone, Zone::Base);
    EXPECT_TRUE(s.obj(m).exhausted);  // state unaffected
    EXPECT_TRUE(s.triggered.empty());  // no Moved event
}

// ---- Showdowns ----

TEST(RiftboundShowdown, EmptyBattlefieldIsConquered) {  // 344.2, 345, 347.2.a, 348.2.a, 471.1
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    move_units(s, 1, {put(s, p, kTwo, Zone::Base)});
    ASSERT_TRUE(s.showdown);
    EXPECT_FALSE(s.showdown->combat);
    EXPECT_EQ(turn_state(s), TurnState::ShowdownOpen);
    EXPECT_EQ(s.decision.player, p);  // focus to whoever contested
    EXPECT_EQ(legal_actions(s).back(), Action{ActionKind::Pass});  // no EndTurn, no moves
    EXPECT_FALSE(offered(s, move_action(s, to_base)));
    pass(s);
    EXPECT_EQ(s.decision.player, o);
    expect_ok(s);
    pass(s);
    EXPECT_FALSE(s.showdown);
    EXPECT_EQ(s.battlefields[1].controller, p);
    EXPECT_FALSE(s.battlefields[1].contested);
    EXPECT_EQ(s.player(p).points, 1);
    EXPECT_EQ(s.player(p).scored, 1 << 1);
    EXPECT_EQ(s.decision.player, p);  // back to the main phase
    expect_ok(s);
}

TEST(RiftboundShowdown, AChainFromAPlayPassesFocus) {  // 346, 347.1.b
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    move_units(s, 1, {put(s, p, kTwo, Zone::Base)});
    play(s, put(s, p, kQuick, Zone::Hand));
    EXPECT_EQ(turn_state(s), TurnState::ShowdownClosed);
    both_pass(s);
    EXPECT_EQ(s.decision.player, o);  // focus passed when the chain closed
    pass(s);                          // o passes focus …
    EXPECT_EQ(s.decision.player, p);
    EXPECT_TRUE(s.showdown);          // … but p's play broke the sequence: p must pass too
    pass(s);
    EXPECT_FALSE(s.showdown);
    expect_ok(s);
}

TEST(RiftboundShowdown, BecomesACombatWhenTheDefenderArrives) {  // 323.13, 466.2.c.1.b, 468.5.e
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId a = put(s, p, kTwo, Zone::Base);
    const ObjId d = put(s, o, kThree, Zone::Base);
    move_units(s, 1, {a});
    pass(s);  // focus to o
    play(s, put(s, o, kCharge, Zone::Hand));  // the only target: d
    both_pass(s);
    EXPECT_EQ(s.obj(d).where, 1);
    ASSERT_TRUE(s.showdown);
    EXPECT_TRUE(s.showdown->combat);
    EXPECT_EQ(s.showdown->attacker, p);  // applied Contested
    EXPECT_EQ(s.obj(a).designation, Designation::Attacker);
    EXPECT_EQ(s.obj(d).designation, Designation::Defender);
    EXPECT_EQ(s.decision.player, p);  // o's chain closed: focus passed on
    expect_ok(s);
    both_pass(s);
    EXPECT_EQ(s.obj(a).zone, Zone::Trash);
    EXPECT_EQ(s.obj(d).damage, 0);  // 2 damage, healed in the Combat Cleanup
    EXPECT_EQ(s.battlefields[1].controller, o);  // the defender establishes control and conquers
    EXPECT_EQ(s.player(o).points, 1);
    expect_ok(s);
}

TEST(RiftboundShowdown, TurnPlayerPicksWhichBegins) {  // 323.11
    GameState s = started();
    const PlayerId p = s.first_player;
    put(s, p, kTwo, Zone::Battlefield, 0);
    put(s, p, kTwo, Zone::Battlefield, 1);
    s.battlefields[0].contested = p;
    s.battlefields[1].contested = p;
    play(s, put(s, p, kQuick, Zone::Hand));  // closed: nothing begins
    both_pass(s);                             // open again: both are staged
    ASSERT_EQ(s.decision.purpose, Purpose::Stage);
    EXPECT_EQ(s.decision.player, p);
    EXPECT_EQ(s.decision.options.size(), 2u);
    apply(s, s.decision.options[1]);
    ASSERT_TRUE(s.showdown);
    EXPECT_EQ(s.showdown->where, 1);
    both_pass(s);  // the other one next
    ASSERT_TRUE(s.showdown);
    EXPECT_EQ(s.showdown->where, 0);
    both_pass(s);
    EXPECT_EQ(s.player(p).points, 2);
    expect_ok(s);
}

// ---- Combat ----

TEST(RiftboundCombat, AttackerWinsAndConquers) {  // 461–468, 471.1
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId d = put(s, o, kTwo, Zone::Battlefield, 0);
    holds(s, o, 0);
    const ObjId a = put(s, p, kThree, Zone::Base);
    move_units(s, 0, {a});
    ASSERT_TRUE(s.showdown);
    EXPECT_TRUE(s.showdown->combat);
    EXPECT_EQ(s.showdown->attacker, p);
    EXPECT_EQ(s.showdown->focus, p);  // 466.2.c.1.a
    EXPECT_EQ(s.obj(a).designation, Designation::Attacker);
    EXPECT_EQ(s.obj(d).designation, Designation::Defender);
    expect_ok(s);
    both_pass(s);
    EXPECT_EQ(s.obj(d).zone, Zone::Trash);
    EXPECT_EQ(s.obj(a).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(a).damage, 0);  // 468.1.a.1
    EXPECT_EQ(s.obj(a).designation, Designation::None);  // 468.7.a
    EXPECT_FALSE(s.showdown);
    EXPECT_EQ(s.battlefields[0].controller, p);
    EXPECT_FALSE(s.battlefields[0].contested);
    EXPECT_EQ(s.player(p).points, 1);
    EXPECT_EQ(s.decision.player, p);
    expect_ok(s);
}

TEST(RiftboundCombat, DrawOutRecallsTheAttackers) {  // 424.1.b, 468.1.a.2, 468.3.d
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId d = put(s, o, kFive, Zone::Battlefield, 0);
    holds(s, o, 0);
    s.obj(d).stunned = true;  // deals no combat damage
    const ObjId a = put(s, p, kTwo, Zone::Base);
    move_units(s, 0, {a});
    both_pass(s);
    EXPECT_EQ(s.obj(a).zone, Zone::Base);  // both survived: the attacker goes home
    EXPECT_TRUE(s.obj(a).exhausted);
    EXPECT_EQ(s.obj(d).damage, 0);
    EXPECT_EQ(s.battlefields[0].controller, o);
    EXPECT_FALSE(s.battlefields[0].contested);
    EXPECT_EQ(s.player(p).points, 0);
    EXPECT_EQ(s.player(o).points, 0);  // already controlled: no conquer
    EXPECT_FALSE(s.showdown);
    expect_ok(s);
}

TEST(RiftboundCombat, MutualDestructionLeavesItUncontrolled) {  // 468.5.b; Assault 807, Shield 814
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId d = put(s, o, kGuard, Zone::Battlefield, 0);
    holds(s, o, 0);
    const ObjId a = put(s, p, kRaider, Zone::Base);
    EXPECT_EQ(might(s, a), 1);
    EXPECT_EQ(might(s, d), 2);
    move_units(s, 0, {a});
    EXPECT_EQ(might(s, a), 3);  // Assault 2 while an attacker
    EXPECT_EQ(might(s, d), 3);  // Shield 1 while a defender
    both_pass(s);
    EXPECT_EQ(s.obj(a).zone, Zone::Trash);
    EXPECT_EQ(s.obj(d).zone, Zone::Trash);
    EXPECT_FALSE(s.battlefields[0].controller);
    EXPECT_FALSE(s.battlefields[0].contested);
    EXPECT_EQ(s.player(p).points + s.player(o).points, 0);
    expect_ok(s);
}

TEST(RiftboundCombat, AssigningIsADecision) {  // 467.2.c.3: 5 damage to two 3s kills one
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId d1 = put(s, o, kThree, Zone::Battlefield, 0);
    const ObjId d2 = put(s, o, kThree, Zone::Battlefield, 0);
    holds(s, o, 0);
    const ObjId a = put(s, p, kFive, Zone::Base);
    move_units(s, 0, {a});
    both_pass(s);
    ASSERT_EQ(s.decision.purpose, Purpose::Assign);
    EXPECT_EQ(s.decision.player, p);
    EXPECT_EQ(legal_actions(s), picks({d1, d2}));
    pick(s, d2);  // the defender's 6 onto the only attacker: no decision
    EXPECT_EQ(s.obj(d2).zone, Zone::Trash);
    EXPECT_EQ(s.obj(d1).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(d1).damage, 0);
    EXPECT_EQ(s.obj(a).zone, Zone::Trash);
    EXPECT_EQ(s.battlefields[0].controller, o);
    expect_ok(s);
}

TEST(RiftboundCombat, LethalFirstNoOverkill) {  // 467.2.c.4: marked damage counts; one path per outcome
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    std::vector<ObjId> d;
    for (int i = 0; i < 3; ++i) d.push_back(put(s, o, kThree, Zone::Battlefield, 0));
    for (const ObjId id : d) s.obj(id).damage = 1;  // 2 more is lethal
    holds(s, o, 0);
    move_units(s, 0, {put(s, p, kFive, Zone::Base)});
    both_pass(s);
    // d[2] first would leave d[0] and d[1] with 3 damage for them both: only reachable in id order
    EXPECT_EQ(legal_actions(s), picks({d[0], d[1]}));
    pick(s, d[0]);
    EXPECT_EQ(legal_actions(s), picks({d[1], d[2]}));
    pick(s, d[2]);  // 1 left: d[1] gets it, not lethal
    EXPECT_EQ(s.obj(d[0]).zone, Zone::Trash);
    EXPECT_EQ(s.obj(d[1]).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(d[2]).zone, Zone::Trash);
    expect_ok(s);
}

TEST(RiftboundCombat, TankTakesDamageFirst) {  // 815.1.b
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId plain = put(s, o, kTwo, Zone::Battlefield, 0);
    const ObjId t = put(s, o, kTank, Zone::Battlefield, 0);
    holds(s, o, 0);
    const ObjId a = put(s, p, kTwo, Zone::Base);
    move_units(s, 0, {a});
    both_pass(s);  // 2 damage: the tank only, no decision
    EXPECT_EQ(s.obj(t).zone, Zone::Trash);
    EXPECT_EQ(s.obj(plain).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(a).zone, Zone::Trash);
    expect_ok(s);
}

TEST(RiftboundCombat, AttackTriggerKeepsFocus) {  // 466.2.d–e, 346.1
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    put(s, o, kTwo, Zone::Battlefield, 0);
    holds(s, o, 0);
    const auto hand = s.player(p).hand.size();
    move_units(s, 0, {put(s, p, kReveler, Zone::Base)});
    ASSERT_EQ(s.chain.size(), 1u);  // the combat chain
    EXPECT_EQ(turn_state(s), TurnState::ShowdownClosed);
    both_pass(s);
    EXPECT_EQ(s.player(p).hand.size(), hand + 1);
    EXPECT_EQ(s.decision.player, p);
    EXPECT_TRUE(s.showdown->window);
    expect_ok(s);
}

TEST(RiftboundCombat, LateArrivalsGetDesignated) {  // 466.2.c.3.a, 323.2.a
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    put(s, o, kFive, Zone::Battlefield, 0);
    holds(s, o, 0);
    move_units(s, 0, {put(s, p, kTwo, Zone::Base)});
    const ObjId late = put(s, p, kThree, Zone::Base);
    play(s, put(s, p, kCharge, Zone::Hand));
    both_pass(s);
    EXPECT_EQ(s.obj(late).designation, Designation::Attacker);
    expect_ok(s);
}

TEST(RiftboundCombat, CombatEffectsExpire) {  // 468.7.c
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    put(s, o, kTwo, Zone::Battlefield, 0);
    holds(s, o, 0);
    const ObjId a = put(s, p, kThree, Zone::Base);
    move_units(s, 0, {a});
    give(s, {.subject = s.obj(a).ref(a), .source = {}, .kind = ModKind::Might, .value = 2, .duration = Duration::Combat});
    give(s, {.subject = s.obj(a).ref(a), .source = {}, .kind = ModKind::Might, .value = 1, .duration = Duration::Turn});
    s.cleanup_due = false;
    EXPECT_EQ(might(s, a), 6);
    both_pass(s);
    EXPECT_EQ(might(s, a), 4);
}

// ---- Scoring ----

TEST(RiftboundScore, ConquerExcessDamage) {  // 467.2.c.4, 473.2.a
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    put(s, o, kTwo, Zone::Battlefield, 0);
    holds(s, o, 0);
    const auto hand = s.player(p).hand.size();
    move_units(s, 0, {put(s, p, kYeti, Zone::Base)});
    both_pass(s);  // 6 against a 2: 4 excess
    ASSERT_EQ(s.chain.size(), 1u);  // the conquer trigger
    both_pass(s);
    EXPECT_EQ(s.player(p).hand.size(), hand + 1);
    expect_ok(s);
}

TEST(RiftboundScore, OncePerBattlefieldPerTurn) {  // 472
    GameState s = started();
    const PlayerId p = s.first_player;
    s.player(p).scored = 1 << 1;
    move_units(s, 1, {put(s, p, kTwo, Zone::Base)});
    both_pass(s);
    EXPECT_EQ(s.battlefields[1].controller, p);
    EXPECT_EQ(s.player(p).points, 0);
}

TEST(RiftboundScore, FinalPointNeedsEveryBattlefield) {  // 473.1.b
    GameState s = started();
    const PlayerId p = s.first_player;
    s.player(p).points = victory_score - 1;
    const auto hand = s.player(p).hand.size();
    move_units(s, 1, {put(s, p, kTwo, Zone::Base)});
    both_pass(s);
    EXPECT_EQ(s.player(p).points, victory_score - 1);  // a card instead
    EXPECT_EQ(s.player(p).hand.size(), hand + 1);
    EXPECT_FALSE(s.winner);

    move_units(s, 0, {put(s, p, kTwo, Zone::Base)});  // now every battlefield this turn
    both_pass(s);
    EXPECT_EQ(s.winner, p);
}

TEST(RiftboundScore, HoldInTheBeginningPhase) {  // 315.2.b, 471.2, 473.1.a.1: Hold isn't held to the final point rule
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    put(s, p, kTwo, Zone::Battlefield, 0);
    holds(s, p, 0);
    apply(s, {ActionKind::EndTurn});
    EXPECT_EQ(s.player(o).points, 0);
    s.player(p).points = victory_score - 1;
    apply(s, {ActionKind::EndTurn});
    EXPECT_EQ(s.winner, p);
}

TEST(RiftboundScore, UnitsEnterWhereTheyArePlayed) {  // 355.2.a
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId first = put(s, p, kTwo, Zone::Hand);
    play(s, first);  // no battlefield controlled: the base, no decision
    EXPECT_EQ(s.obj(first).zone, Zone::Base);
    put(s, p, kThree, Zone::Battlefield, 1);
    holds(s, p, 1);
    const ObjId second = put(s, p, kTwo, Zone::Hand);
    play(s, second);
    ASSERT_EQ(s.decision.purpose, Purpose::Location);
    EXPECT_EQ(s.decision.options, (std::vector<Action>{{ActionKind::Pick, s.battlefields[1].card, 1},
                                                       {ActionKind::Pick, no_obj, to_base}}));
    apply(s, s.decision.options[0]);
    EXPECT_EQ(s.obj(second).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(second).where, 1);
    EXPECT_TRUE(s.obj(second).exhausted);
    EXPECT_FALSE(s.battlefields[1].contested);
    expect_ok(s);
}
