// Abilities (360–406), targeting (355.5–355.17, 359.3.e), costs (356), layers (475–482), durations, making new
// choices (750). One test card per ability kind, shaped like the decks' cards it stands for.
#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>

#include "riftbound/actions.hpp"
#include "riftbound/engine.hpp"
#include "riftbound/random_game.hpp"
#include "riftbound/rules.hpp"
#include "test_cards.hpp"

using namespace buddy::riftbound;

namespace {

const Card* make(std::string name, CardType type, int energy, int might, const CardRules& rules,
                 Timing timing = Timing::Normal) {
    Card c;
    c.name = std::move(name);
    c.type = type;
    c.domains = domain::fury;
    c.energy = energy;
    if (type == CardType::Unit) c.might = might;
    if (type == CardType::Spell) c.power = 0;
    c.timing = timing;
    c.rules = &rules;
    return test::card(std::move(c));
}

Cause by(Resolution& r) { return {r.source(), r.controller()}; }

int count_named(const GameState& s, const std::vector<ObjId>& zone, const std::string& name) {
    return static_cast<int>(std::count_if(zone.begin(), zone.end(), [&](ObjId id) { return s.obj(id).card->name == name; }));
}

// ---- The test cards ----

// Hidden Blade's shape: "Deal 3 to a unit at a battlefield. Its controller draws 1." (linked, 359.3.e.14.a)
struct Bolt : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit_at_battlefield}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    Status resolve(Resolution& r) const override {
        const ObjId t = r.target();
        if (t == no_obj) return Status::Done;  // ignored, and the linked draw with it
        const PlayerId controller = r.s.obj(t).controller;
        deal(r.s, t, 3, by(r));
        draw(r.s, controller, 1);
        return Status::Done;
    }
} const bolt;

// "[Reaction] Recall a unit."
struct Shove : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) recall(r.s, t);
        return Status::Done;
    }
} const shove;

// Loyal Poro: "[Deathknell] If I didn't die alone, draw 1."
struct Poro : CardRules {
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
        if (!r.item().event.alone) draw(r.s, r.controller(), 1);  // noted when it died (808.1.d.3)
        return Status::Done;
    }
} const poro;

// Tactical Retreat: "[Reaction] Choose a friendly unit. The next time it would die this turn, heal it, exhaust it,
// and recall it instead." A delayed replacement (390, 367).
struct Retreat : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::friendly_unit}};
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Replacement}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) r.delay(0, r.s.obj(t).ref(t));
        return Status::Done;
    }
    bool replace(GameState& s, Event& e, ObjId, std::uint8_t, const Delayed* d) const override {
        if (d == nullptr || e.kind != EventKind::Die || e.subject != d->subject) return false;
        heal(s, e.subject.id);
        exhaust(s, e.subject.id);
        recall(s, e.subject.id);
        e.cancelled = true;
        return true;
    }
} const retreat;

// Arena Kingpin: "I enter ready. [E]: Give a unit +3 Might this turn."
struct Kingpin : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.exhaust_self = true;
        a.targets = kTargets;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    bool enters_ready(const GameState&, ObjId, const ChainItem&) const override { return true; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) give_might(r.s, t, 3, r.source(), Duration::Turn);
        return Status::Done;
    }
} const kingpin;

// Divining Shells: "[Action] Kill this, [E]: Give a unit +2 Might this turn."
struct Shells : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.exhaust_self = a.cost.kill_self = true;
        a.targets = kTargets;
        a.timing = Timing::Action;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) give_might(r.s, t, 2, r.source(), Duration::Turn);
        return Status::Done;
    }
} const shells;

// A conditional passive (364.3.a): "I have +2 Might while you have 2 or more cards in hand."
struct Hoarder : CardRules {
    void passive(const GameState& s, ObjId self, ObjId subject, const Traits&, std::vector<Mod>& out) const override {
        if (subject == self && s.player(s.obj(self).controller).hand.size() >= 2) out.push_back({ModKind::Might, {}, 2});
    }
} const hoarder;

// A keyword grant to others (479.3): "Other friendly units have [Ganking]."
struct Banner : CardRules {
    void passive(const GameState& s, ObjId self, ObjId subject, const Traits&, std::vector<Mod>& out) const override {
        const Object& o = s.obj(subject);
        if (subject != self && on_board(o.zone) && o.card->type == CardType::Unit &&
            o.controller == s.obj(self).controller) {
            out.push_back({ModKind::Grant, Keyword::Ganking, 1});
        }
    }
} const banner;

// Fiora's shape (478.4): "While I'm Mighty, I have [Shield] and +1 Might." Needs the layers re-evaluated.
struct Giant : CardRules {
    void passive(const GameState&, ObjId self, ObjId subject, const Traits& now, std::vector<Mod>& out) const override {
        if (subject != self || now.might < 5) return;
        out.push_back({ModKind::Grant, Keyword::Shield, 1});
        out.push_back({ModKind::Might, {}, 1});
    }
} const giant;

// 481's example: "Units you control have their Might increased to 5."
struct Anthem : CardRules {
    void passive(const GameState& s, ObjId self, ObjId subject, const Traits&, std::vector<Mod>& out) const override {
        const Object& o = s.obj(subject);
        if (on_board(o.zone) && o.card->type == CardType::Unit && o.controller == s.obj(self).controller) {
            out.push_back({ModKind::RaiseMight, {}, 5});
        }
    }
} const anthem;

// Shadowblade Lurker: "I cost [2] less for each card with my name in your trash."
struct Lurker : CardRules {
    void cost_mods(const GameState& s, ObjId self, const ChainItem& item, std::vector<CostMod>& out) const override {
        if (item.card != self) return;
        const Object& o = s.obj(self);
        const int n = count_named(s, s.player(o.owner).trash, o.card->name);
        out.push_back({CostMod::Discount, 2 * n, 0, 0});
    }
} const lurker;

// Eager Apprentice's shape: "Spells you play cost [1] less, to a minimum of [1]." (356.4.e)
struct Apprentice : CardRules {
    void cost_mods(const GameState& s, ObjId self, const ChainItem& item, std::vector<CostMod>& out) const override {
        if (item.kind == ItemKind::Card && s.obj(item.card).card->type == CardType::Spell &&
            item.controller == s.obj(self).controller) {
            out.push_back({CostMod::Discount, 1, 0, 1});
        }
    }
} const apprentice;

// Ruthless Strike: "[Action] As an additional cost to play this, you may discard 1. Deal 3 to a unit at a
// battlefield. If you paid the additional cost, deal 5 to it instead."
struct Strike : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit_at_battlefield}};
    static constexpr Cost kOptional[] = {[] {
        Cost c;
        c.discard = 1;
        return c;
    }()};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    std::span<const Cost> optional_costs() const override { return kOptional; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) deal(r.s, t, (r.item().paid & 1) ? 5 : 3, by(r));
        return Status::Done;
    }
} const strike;

// Sharkling: "[Accelerate] (You may pay [1][Fury] as an additional cost to have me enter ready.)" — [1] here.
struct Sharkling : CardRules {
    static constexpr Cost kOptional[] = {{1}};
    std::span<const Cost> optional_costs() const override { return kOptional; }
    bool enters_ready(const GameState&, ObjId, const ChainItem& played) const override { return played.paid & 1; }
} const sharkling;

// "When you play a spell, you may give me +1 Might this turn. Do this only once each turn." (383.3.a, 386)
struct Fan : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.may = true;
        a.per_turn = 1;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Played && e.player == s.obj(self).controller &&
            s.obj(e.subject.id).card->type == CardType::Spell) {
            out.push_back(0);
        }
    }
    Status resolve(Resolution& r) const override {
        if (const ObjId me = r.self(); me != no_obj) give_might(r.s, me, 1, r.source(), Duration::Turn);
        return Status::Done;
    }
} const fan;

// "At the start of your Beginning Phase, draw 1." (315.2.a)
struct Scholar : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::PhaseStart && e.amount == static_cast<int>(Phase::Beginning) &&
            e.player == s.obj(self).controller) {
            out.push_back(0);
        }
    }
    Status resolve(Resolution& r) const override {
        draw(r.s, r.controller(), 1);
        return Status::Done;
    }
} const scholar;

// A reflexive trigger (387): "Draw 1. Do this: deal 1 to a unit."
struct Echo : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.targets = kTargets;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Status resolve(Resolution& r) const override {
        if (r.ability() == card_effect) {
            draw(r.s, r.controller(), 1);
            r.reflexive(0);
        } else if (const ObjId t = r.target(); t != no_obj) {
            deal(r.s, t, 1, by(r));
        }
        return Status::Done;
    }
} const echo;

// A delayed trigger (391.2): "At the end of this turn, draw 1."
struct Later : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    bool delayed_trigger(const GameState&, const Event& e, const Delayed&) const override {
        return e.kind == EventKind::PhaseStart && e.amount == static_cast<int>(Phase::Ending);
    }
    Status resolve(Resolution& r) const override {
        if (r.ability() == card_effect) {
            r.delay(0);
        } else {
            draw(r.s, r.controller(), 1);
        }
        return Status::Done;
    }
} const later;

// 750: "[Reaction] Choose a spell. Make new choices for it."
struct Redirect : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::spell}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    Status resolve(Resolution& r) const override {
        const ObjId t = r.target();
        if (t == no_obj) return Status::Done;
        bool negated = false;
        if (r.new_choices(t, negated) == Status::Wait) return r.wait(r.step());
        r.var(0) = negated;
        return Status::Done;
    }
} const redirect;

// Blade Twirler's shape, a player target: "When you play me, choose a player. They draw 1."
struct Twirler : CardRules {
    static constexpr TargetSpec kTargets[] = {[] {
        TargetSpec t;
        t.filter = filters::player;
        t.players = true;
        return t;
    }()};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.targets = kTargets;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Played && e.subject.id == self) out.push_back(0);  // a play effect (383.4.a)
    }
    Status resolve(Resolution& r) const override {
        if (const auto p = r.target_player()) draw(r.s, *p, 1);
        return Status::Done;
    }
} const twirler;

// Traveling Merchant's effect as a spell, a choice on resolution (355.18): "Discard 1, then draw 1."
struct Merchant : CardRules {
    Status resolve(Resolution& r) const override {
        const PlayerId p = r.controller();
        if (r.step() == 0) {
            std::vector<Action> hand;
            for (const ObjId id : r.s.player(p).hand) hand.push_back({ActionKind::Pick, id});
            std::sort(hand.begin(), hand.end(), [](const Action& a, const Action& b) { return a.obj < b.obj; });
            if (!hand.empty()) {
                r.ask(p, std::move(hand), 1, 1);
                return r.wait(1);
            }
        } else {
            discard(r.s, r.answer().front().obj, by(r));
        }
        draw(r.s, p, 1);
        return Status::Done;
    }
} const merchant;

// Linked abilities (394–398): "[E]: Banish a card from your trash." and "Return the cards banished with this to
// their owner's hand."
struct Vault : CardRules {
    static bool own_trash(const GameState& s, const ChainItem& item, const Target& t) {
        const Object& o = s.obj(t.ref.id);
        return o.zone == Zone::Trash && o.owner == item.controller;
    }
    static constexpr TargetSpec kTargets[] = {{own_trash}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.exhaust_self = true;
        a.targets = kTargets;
        return a;
    }(), [] {
        AbilitySpec a;
        a.usable = [](const GameState& s, ObjId self) { return !linked(s, s.obj(self).ref(self)).empty(); };
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Status resolve(Resolution& r) const override {
        const ObjId me = r.self();
        if (me == no_obj) return Status::Done;
        if (r.ability() == 0) {
            if (const ObjId t = r.target(); t != no_obj) {
                move_to(r.s, t, Zone::Banishment, r.s.obj(t).owner);
                link(r.s, r.source(), r.s.obj(t).ref(t));
            }
        } else {
            for (const ObjId id : linked(r.s, r.source())) move_to(r.s, id, Zone::Hand, r.s.obj(id).owner);
        }
        return Status::Done;
    }
} const vault;

// Punching Poro's shape plus a "when I become Empowered" trigger (827, 828): "[Empower] [1]. [Empowered] I have
// +1 Might. When I become Empowered, draw 1."
struct Empowerer : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.energy = 1;
        a.usable = [](const GameState& s, ObjId self) { return !s.obj(self).empowered; };
        return a;
    }(), {AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void passive(const GameState& s, ObjId self, ObjId subject, const Traits&, std::vector<Mod>& out) const override {
        if (subject == self && s.obj(self).empowered) out.push_back({ModKind::Might, {}, 1});
    }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Empowered && e.subject.id == self) out.push_back(1);
    }
    Status resolve(Resolution& r) const override {
        if (r.ability() == 0 && r.self() != no_obj) empower(r.s, r.self());
        if (r.ability() == 1) draw(r.s, r.controller(), 1);
        return Status::Done;
    }
} const empowerer;

// Zed, Master of Shadows' shape: "When you discard a card, empower me. [Action] Disempower me, [E]: Draw 1."
struct ShadowLegend : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}, [] {
        AbilitySpec a;
        a.cost.disempower_self = a.cost.exhaust_self = true;
        a.timing = Timing::Action;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Discarded && e.player == s.obj(self).owner) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        if (r.ability() == 0 && r.self() != no_obj) empower(r.s, r.self());
        if (r.ability() == 1) draw(r.s, r.controller(), 1);
        return Status::Done;
    }
} const shadow_legend;

// 757: "I can't be chosen by enemy spells and abilities."
struct Wall : CardRules {
    bool untargetable(const GameState& s, ObjId self, const ChainItem& by) const override {
        return by.controller != s.obj(self).controller;
    }
} const wall;

const Card* const kBolt = make("Bolt", CardType::Spell, 0, 0, bolt);
const Card* const kShove = make("Shove", CardType::Spell, 0, 0, shove, Timing::Reaction);
const Card* const kPoro = make("Poro", CardType::Unit, 0, 2, poro);
const Card* const kRetreat = make("Retreat", CardType::Spell, 0, 0, retreat, Timing::Reaction);
const Card* const kKingpin = make("Kingpin", CardType::Unit, 0, 3, kingpin);
const Card* const kShells = make("Shells", CardType::Gear, 0, 0, shells);
const Card* const kHoarder = make("Hoarder", CardType::Unit, 0, 1, hoarder);
const Card* const kBanner = make("Banner", CardType::Gear, 0, 0, banner);
const Card* const kGiant = make("Giant", CardType::Unit, 0, 4, giant);
const Card* const kAnthem = make("Anthem", CardType::Gear, 0, 0, anthem);
const Card* const kLurker = make("Lurker", CardType::Unit, 5, 5, lurker);
const Card* const kApprentice = make("Apprentice", CardType::Gear, 0, 0, apprentice);
const Card* const kPricey = make("Pricey Spell", CardType::Spell, 2, 0, test::plain_rules);
const Card* const kStrike = make("Strike", CardType::Spell, 0, 0, strike, Timing::Action);
const Card* const kSharkling = make("Sharkling", CardType::Unit, 1, 1, sharkling);
const Card* const kFan = make("Fan", CardType::Unit, 0, 1, fan);
const Card* const kScholar = make("Scholar", CardType::Unit, 0, 1, scholar);
const Card* const kEcho = make("Echo", CardType::Spell, 0, 0, echo);
const Card* const kLater = make("Later", CardType::Spell, 0, 0, later);
const Card* const kRedirect = make("Redirect", CardType::Spell, 0, 0, redirect, Timing::Reaction);
const Card* const kTwirler = make("Twirler", CardType::Unit, 0, 2, twirler);
const Card* const kMerchant = make("Merchant", CardType::Spell, 0, 0, merchant);
const Card* const kVault = make("Vault", CardType::Gear, 0, 0, vault);
const Card* const kEmpowerer = make("Empowerer", CardType::Unit, 0, 2, empowerer);
const Card* const kWall = make("Wall", CardType::Unit, 0, 3, wall);
const Card* const kShadowLegend = make("Shadow Legend", CardType::Legend, 0, 0, shadow_legend);

const std::vector<const Card*>& all_cards() {
    static const std::vector<const Card*> cards = {
        kBolt, kShove, kPoro, kRetreat, kKingpin, kShells, kHoarder, kBanner, kGiant, kAnthem, kLurker,
        kApprentice, kPricey, kStrike, kSharkling, kFan, kScholar, kEcho, kLater, kRedirect, kTwirler,
        kMerchant, kVault, kEmpowerer, kWall};
    return cards;
}

// Every test card twice (Lurker 4×), the Shadow Legend, the rest plain units: a rules deck for both seats.
const Deck& ability_deck() {
    static const Deck d = [] {
        Deck deck = test::ruled(test::simple_deck("Q"));
        deck.legend = kShadowLegend;
        deck.main.clear();
        for (const Card* c : all_cards()) deck.main.push_back({c, c == kLurker ? 4 : 2});
        return deck;
    }();
    return d;
}

// Both keep: turn 1's priority for the first player, who has 2 runes.
GameState started(std::uint64_t seed = 5) {
    GameState s = new_game(ability_deck(), ability_deck(), {.seed = seed});
    apply(s, {ActionKind::Done});
    apply(s, {ActionKind::Done});
    return s;
}

// A copy of `card` of player p out of the deck (or wherever) into `zone`. Board zones: p controls it.
ObjId put(GameState& s, PlayerId p, const Card* card, Zone zone, std::uint8_t where = 0) {
    for (ObjId id = 0; id < s.objects.size(); ++id) {
        const Object& o = s.obj(id);
        if (o.card == card && o.owner == p && o.zone != zone && !on_board(o.zone) && o.zone != Zone::Hand &&
            o.zone != Zone::Chain) {
            move_to(s, id, zone, p, where);
            s.cleanup_due = false;
            return id;
        }
    }
    for (const ObjId id : s.player(p).hand) {  // only copy left is in the opening hand
        if (s.obj(id).card == card && zone != Zone::Hand) {
            move_to(s, id, zone, p, where);
            s.cleanup_due = false;
            return id;
        }
    }
    throw std::logic_error("no copy left of " + card->name);
}

bool offered(const GameState& s, const Action& a) {
    const auto actions = legal_actions(s);
    return std::find(actions.begin(), actions.end(), a) != actions.end();
}

void play(GameState& s, ObjId id) { apply(s, {ActionKind::Play, id}); }
void pass(GameState& s) { apply(s, {ActionKind::Pass}); }
void pick(GameState& s, ObjId id) { apply(s, {ActionKind::Pick, id}); }
void both_pass(GameState& s) {
    pass(s);
    pass(s);
}

// Answers everything with the first option and passes until the chain is empty and the turn player may act.
void settle(GameState& s) {
    while (!s.chain.empty() || s.decision.kind != DecisionKind::Priority) {
        if (s.decision.kind == DecisionKind::Choose) apply(s, legal_actions(s).front());
        else pass(s);
    }
}

void expect_ok(const GameState& s) {
    const auto problems = check_invariants(s);
    EXPECT_TRUE(problems.empty()) << problems.front();
}

} // namespace

// ---- Targeting ----

TEST(RiftboundTargets, SpellHitsItsTargetAndLinkedInstruction) {  // 355.5, 359.3.d
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId unit = put(s, o, kKingpin, Zone::Battlefield);
    const auto hand = s.player(o).hand.size();
    play(s, put(s, p, kBolt, Zone::Hand));  // the only candidate: chosen without a decision
    ASSERT_EQ(s.chain.size(), 1u);
    EXPECT_EQ(s.chain[0].targets.size(), 1u);
    both_pass(s);
    EXPECT_EQ(s.obj(unit).zone, Zone::Trash);  // 3 ≥ 3 Might: dies in the cleanup after the resolution
    EXPECT_EQ(s.player(o).hand.size(), hand + 1);
    expect_ok(s);
}

TEST(RiftboundTargets, ChoosingAmongSeveral) {  // 355.5: one decision, options in id order
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId a = put(s, o, kKingpin, Zone::Battlefield);
    const ObjId b = put(s, p, kGiant, Zone::Battlefield);
    play(s, put(s, p, kBolt, Zone::Hand));
    ASSERT_EQ(s.decision.kind, DecisionKind::Choose);
    EXPECT_EQ(s.decision.purpose, Purpose::Target);
    EXPECT_EQ(s.decision.player, p);
    const auto actions = legal_actions(s);
    ASSERT_EQ(actions.size(), 2u);  // exactly one target: no Done
    EXPECT_EQ(actions[0].obj, std::min(a, b));
    expect_ok(s);
    pick(s, b);
    EXPECT_EQ(s.chain[0].targets.front().ref.id, b);
    EXPECT_TRUE(s.chain[0].finalized);
    EXPECT_EQ(s.decision.kind, DecisionKind::Priority);
}

TEST(RiftboundTargets, NoLegalTargetNoPlay) {  // 355.8
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId bolt_id = put(s, p, kBolt, Zone::Hand);
    put(s, opponent(p), kKingpin, Zone::Base);  // a unit, but not at a battlefield
    EXPECT_FALSE(offered(s, {ActionKind::Play, bolt_id}));
}

TEST(RiftboundTargets, MistargetIgnoresLinkedInstructions) {  // 359.3.e.5, 359.3.e.14.a
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId unit = put(s, o, kKingpin, Zone::Battlefield);
    const ObjId answer = put(s, o, kShove, Zone::Hand);
    play(s, put(s, p, kBolt, Zone::Hand));
    pass(s);
    play(s, answer);  // recalls the target to base: no longer "at a battlefield"
    both_pass(s);
    EXPECT_EQ(s.obj(unit).zone, Zone::Base);
    const auto hand = s.player(o).hand.size();
    both_pass(s);  // Bolt resolves, mistargeted
    EXPECT_EQ(s.obj(unit).damage, 0);
    EXPECT_EQ(s.player(o).hand.size(), hand);  // the linked draw is ignored too
    EXPECT_TRUE(s.chain.empty());
    expect_ok(s);
}

TEST(RiftboundTargets, NewObjectIsNotTheTarget) {  // 359.3.e.4: hand and back is a new object
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId unit = put(s, o, kKingpin, Zone::Battlefield);
    play(s, put(s, p, kBolt, Zone::Hand));
    move_to(s, unit, Zone::Hand, o);
    move_to(s, unit, Zone::Battlefield, o);
    both_pass(s);
    EXPECT_EQ(s.obj(unit).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(unit).damage, 0);
}

TEST(RiftboundTargets, UntargetableByEnemies) {  // 757, 758
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    put(s, o, kWall, Zone::Battlefield);
    const ObjId bolt_id = put(s, p, kBolt, Zone::Hand);
    EXPECT_FALSE(offered(s, {ActionKind::Play, bolt_id}));  // the only unit can't be chosen by p
    const ObjId mine = put(s, p, kWall, Zone::Battlefield);
    play(s, bolt_id);  // p's own Wall can be chosen
    EXPECT_EQ(s.chain[0].targets.front().ref.id, mine);
}

TEST(RiftboundTargets, PlayerTarget) {  // 355.7: players as targets, a play effect (383.4.a)
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const auto hand = s.player(o).hand.size();
    play(s, put(s, p, kTwirler, Zone::Hand));
    ASSERT_EQ(s.chain.size(), 1u);  // the trigger, pending: its target is chosen now (355.5.b)
    EXPECT_EQ(s.chain[0].kind, ItemKind::Triggered);
    ASSERT_EQ(s.decision.kind, DecisionKind::Choose);
    EXPECT_EQ(legal_actions(s).size(), 2u);
    apply(s, {ActionKind::Pick, no_obj, o});
    both_pass(s);
    EXPECT_EQ(s.player(o).hand.size(), hand + 1);
    expect_ok(s);
}

TEST(RiftboundTargets, NewChoices) {  // 750–755
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId a = put(s, o, kKingpin, Zone::Battlefield);
    const ObjId b = put(s, o, kGiant, Zone::Battlefield);
    const ObjId bolt_id = put(s, p, kBolt, Zone::Hand);
    const ObjId redirect_id = put(s, o, kRedirect, Zone::Hand);
    play(s, bolt_id);
    pick(s, a);
    pass(s);
    play(s, redirect_id);  // its only candidate is Bolt
    EXPECT_EQ(s.chain[1].targets.front().ref.id, bolt_id);
    both_pass(s);
    ASSERT_EQ(s.decision.kind, DecisionKind::Choose);  // o makes Bolt's choices again, the old one allowed too (753)
    EXPECT_EQ(s.decision.player, o);
    EXPECT_EQ(legal_actions(s).size(), 2u);
    pick(s, b);
    EXPECT_EQ(s.chain.back().targets.front().ref.id, b);
    both_pass(s);
    EXPECT_EQ(s.obj(b).damage, 3);
    EXPECT_EQ(s.obj(a).damage, 0);
    expect_ok(s);
}

TEST(RiftboundTargets, NewChoicesNegatedWithoutLegalChoices) {  // 753.2
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId a = put(s, o, kKingpin, Zone::Battlefield);
    const ObjId bolt_id = put(s, p, kBolt, Zone::Hand);
    play(s, bolt_id);
    pass(s);
    play(s, put(s, o, kShove, Zone::Hand));  // recalls a, the only unit: no unit at a battlefield left
    both_pass(s);
    pass(s);  // p, Bolt's controller, had priority after the resolution
    play(s, put(s, o, kRedirect, Zone::Hand));
    // nothing at a battlefield: new choices are negated, Bolt keeps its old (now illegal) target
    both_pass(s);
    EXPECT_EQ(s.chain.size(), 1u);
    EXPECT_EQ(s.chain[0].targets.front().ref.id, a);
    expect_ok(s);
}

// ---- Triggered abilities ----

TEST(RiftboundTriggers, DeathknellNotesWhetherItDiedAlone) {  // 808, 383.2.c.3
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId poro_id = put(s, o, kPoro, Zone::Battlefield);
    put(s, o, kGiant, Zone::Battlefield);  // not alone
    play(s, put(s, p, kBolt, Zone::Hand));
    pick(s, poro_id);
    const auto hand = s.player(o).hand.size();
    both_pass(s);  // Bolt: 3 damage + its draw; the cleanup kills Poro, its Deathknell goes on the chain
    EXPECT_EQ(s.obj(poro_id).zone, Zone::Trash);
    ASSERT_EQ(s.chain.size(), 1u);
    EXPECT_EQ(s.chain[0].controller, o);
    EXPECT_FALSE(s.chain[0].event.alone);
    EXPECT_EQ(s.decision.player, o);  // the newest item's controller
    expect_ok(s);
    both_pass(s);
    EXPECT_EQ(s.player(o).hand.size(), hand + 2);
}

TEST(RiftboundTriggers, DeathknellAlone) {
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    put(s, o, kPoro, Zone::Battlefield);
    play(s, put(s, p, kBolt, Zone::Hand));
    const auto hand = s.player(o).hand.size();
    both_pass(s);
    both_pass(s);
    EXPECT_EQ(s.player(o).hand.size(), hand + 1);  // only Bolt's draw
}

TEST(RiftboundTriggers, MayTriggerOncePerTurn) {  // 383.3.a, 386.2
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId fan_id = put(s, p, kFan, Zone::Base);
    play(s, put(s, p, kMerchant, Zone::Hand));
    ASSERT_EQ(s.decision.kind, DecisionKind::Choose);  // Fan's trigger: perform it?
    EXPECT_EQ(s.decision.purpose, Purpose::May);
    apply(s, {ActionKind::Done});  // no: removed, not counted
    EXPECT_EQ(s.chain.size(), 1u);
    settle(s);
    play(s, put(s, p, kLater, Zone::Hand));
    ASSERT_EQ(s.decision.purpose, Purpose::May);
    pick(s, fan_id);
    both_pass(s);  // Fan's ability
    EXPECT_EQ(might(s, fan_id), 2);
    both_pass(s);  // Later
    play(s, put(s, p, kLater, Zone::Hand));
    EXPECT_EQ(s.decision.kind, DecisionKind::Priority);  // used this turn: no trigger
    EXPECT_EQ(s.chain.size(), 1u);
    expect_ok(s);
}

TEST(RiftboundTriggers, BeginningPhaseTriggerResolvesBeforeChannel) {  // 315.2.a, 402.2
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    put(s, o, kScholar, Zone::Base);
    const auto hand = s.player(o).hand.size();
    const auto runes = s.player(o).rune_deck.size();
    apply(s, {ActionKind::EndTurn});
    ASSERT_EQ(s.chain.size(), 1u);
    EXPECT_EQ(s.phase, Phase::Beginning);
    EXPECT_EQ(s.decision.player, o);
    EXPECT_EQ(s.player(o).rune_deck.size(), runes);  // channel waits for the chain
    expect_ok(s);
    both_pass(s);
    EXPECT_EQ(s.phase, Phase::Main);
    EXPECT_EQ(s.player(o).hand.size(), hand + 2);  // the trigger, then the draw step
}

TEST(RiftboundTriggers, OwnerOrdersSimultaneousTriggers) {  // 383.3.d
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId a = put(s, o, kScholar, Zone::Base);
    const ObjId b = put(s, o, kScholar, Zone::Base);
    apply(s, {ActionKind::EndTurn});
    ASSERT_EQ(s.decision.kind, DecisionKind::Choose);
    EXPECT_EQ(s.decision.purpose, Purpose::TriggerOrder);
    EXPECT_EQ(s.decision.player, o);
    apply(s, legal_actions(s).back());  // b's first: resolves last
    EXPECT_EQ(s.chain.size(), 2u);
    EXPECT_EQ(s.chain[0].source.id, b);
    EXPECT_EQ(s.chain[1].source.id, a);
    expect_ok(s);
}

TEST(RiftboundTriggers, ReflexiveTriggerAfterTheResolution) {  // 387–389
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId unit = put(s, o, kKingpin, Zone::Base);
    play(s, put(s, p, kEcho, Zone::Hand));
    both_pass(s);
    ASSERT_EQ(s.chain.size(), 1u);  // the reflexive trigger, finalized: its only target chosen
    EXPECT_EQ(s.chain[0].kind, ItemKind::Triggered);
    EXPECT_TRUE(s.chain[0].finalized);
    both_pass(s);
    EXPECT_EQ(s.obj(unit).damage, 1);
    expect_ok(s);
}

TEST(RiftboundTriggers, TriggerWithoutTargetsLeaves) {  // 403.4
    GameState s = started();
    const PlayerId p = s.first_player;
    play(s, put(s, p, kEcho, Zone::Hand));  // no unit anywhere
    both_pass(s);
    EXPECT_TRUE(s.chain.empty());
    EXPECT_EQ(s.decision.kind, DecisionKind::Priority);
}

TEST(RiftboundTriggers, DelayedTriggerAtEndOfTurn) {  // 390–393, 317.1, 317.2.f
    GameState s = started();
    const PlayerId p = s.first_player;
    play(s, put(s, p, kLater, Zone::Hand));
    both_pass(s);
    EXPECT_EQ(s.delayed.size(), 1u);
    const auto hand = s.player(p).hand.size();
    apply(s, {ActionKind::EndTurn});
    ASSERT_EQ(s.chain.size(), 1u);
    EXPECT_EQ(s.phase, Phase::Ending);
    both_pass(s);
    EXPECT_EQ(s.player(p).hand.size(), hand + 1);
    EXPECT_TRUE(s.delayed.empty());
    EXPECT_EQ(s.turn_player, opponent(p));  // Expiration ran again, then the next turn
    expect_ok(s);
}

TEST(RiftboundTriggers, EmpowerTriggersAndDisempowerCost) {  // 827, 828, 442
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId e = put(s, p, kEmpowerer, Zone::Base);
    const ObjId legend = s.player(p).legend.front();
    EXPECT_FALSE(offered(s, {ActionKind::Activate, legend, 1}));  // not Empowered
    ASSERT_TRUE(offered(s, {ActionKind::Activate, e, 0}));
    const auto hand = s.player(p).hand.size();
    apply(s, {ActionKind::Activate, e, 0});
    both_pass(s);
    EXPECT_TRUE(s.obj(e).empowered);
    EXPECT_EQ(might(s, e), 3);  // [Empowered] +1
    EXPECT_FALSE(offered(s, {ActionKind::Activate, e, 0}));  // use only if not Empowered
    both_pass(s);  // "when I become Empowered"
    EXPECT_EQ(s.player(p).hand.size(), hand + 1);

    play(s, put(s, p, kMerchant, Zone::Hand));  // discards: the legend's trigger empowers it
    settle(s);
    EXPECT_TRUE(s.obj(legend).empowered);
    ASSERT_TRUE(offered(s, {ActionKind::Activate, legend, 1}));
    apply(s, {ActionKind::Activate, legend, 1});
    EXPECT_FALSE(s.obj(legend).empowered);  // paid while finalizing
    EXPECT_TRUE(s.obj(legend).exhausted);
    expect_ok(s);
}

// ---- Activated abilities ----

TEST(RiftboundActivated, ExhaustCostTargetAndThisTurn) {  // 376–381, 317.2.c
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId pin = put(s, p, kKingpin, Zone::Hand);
    play(s, pin);
    EXPECT_FALSE(s.obj(pin).exhausted);  // I enter ready (369.3)
    ASSERT_TRUE(offered(s, {ActionKind::Activate, pin, 0}));
    apply(s, {ActionKind::Activate, pin, 0});  // only itself to target
    EXPECT_TRUE(s.obj(pin).exhausted);
    ASSERT_EQ(s.chain.size(), 1u);
    EXPECT_EQ(s.chain[0].card, no_obj);
    EXPECT_EQ(turn_state(s), TurnState::NeutralClosed);  // 377.3.a.1
    expect_ok(s);
    both_pass(s);
    EXPECT_EQ(might(s, pin), 6);
    EXPECT_FALSE(offered(s, {ActionKind::Activate, pin, 0}));  // exhausted
    apply(s, {ActionKind::EndTurn});
    EXPECT_EQ(might(s, pin), 3);  // "this turn" expired
    EXPECT_TRUE(s.effects.empty());
    // 381: not on the opponent's turn
    ready(s, pin);
    s.cleanup_due = false;
    ASSERT_EQ(s.decision.player, o);
    EXPECT_FALSE(offered(s, {ActionKind::Activate, pin, 0}));
}

TEST(RiftboundActivated, KillThisCostAndActionTiming) {  // 806, 357.2
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId gear = put(s, p, kShells, Zone::Base);
    const ObjId unit = put(s, p, kKingpin, Zone::Base);
    play(s, put(s, p, kShove, Zone::Hand));  // closed: [Action] isn't enough
    EXPECT_FALSE(offered(s, {ActionKind::Activate, gear, 0}));
    s = started();
    const ObjId gear2 = put(s, p, kShells, Zone::Base);
    const ObjId unit2 = put(s, p, kKingpin, Zone::Base);
    apply(s, {ActionKind::Activate, gear2, 0});  // Kingpin is the only unit
    EXPECT_EQ(s.obj(gear2).zone, Zone::Trash);  // killed as a cost
    both_pass(s);
    EXPECT_EQ(might(s, unit2), 5);
    (void)unit;
    expect_ok(s);
}

TEST(RiftboundActivated, LinkedAbilities) {  // 394–398
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId v = put(s, p, kVault, Zone::Base);
    const ObjId x = put(s, p, kBolt, Zone::Trash);
    const ObjId y = put(s, p, kShove, Zone::Banishment);  // banished by something else
    EXPECT_FALSE(offered(s, {ActionKind::Activate, v, 1}));  // nothing linked yet
    apply(s, {ActionKind::Activate, v, 0});
    both_pass(s);
    EXPECT_EQ(s.obj(x).zone, Zone::Banishment);
    apply(s, {ActionKind::Activate, v, 1});
    both_pass(s);
    EXPECT_EQ(s.obj(x).zone, Zone::Hand);
    EXPECT_EQ(s.obj(y).zone, Zone::Banishment);  // not banished with this
    expect_ok(s);
}

// ---- Replacement effects and durations ----

TEST(RiftboundReplacement, TacticalRetreatSavesOnce) {  // 367–373, 390, 808.1.d.1
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId poro_id = put(s, o, kPoro, Zone::Battlefield);
    put(s, o, kGiant, Zone::Battlefield);
    const ObjId save = put(s, o, kRetreat, Zone::Hand);
    play(s, put(s, p, kBolt, Zone::Hand));
    pick(s, poro_id);
    pass(s);
    play(s, save);  // only friendly units: Poro or Giant
    pick(s, poro_id);
    both_pass(s);  // Retreat: a delayed replacement
    ASSERT_EQ(s.delayed.size(), 1u);
    const auto hand = s.player(o).hand.size();
    both_pass(s);  // Bolt: 3 damage, lethal; the death is replaced
    EXPECT_EQ(s.obj(poro_id).zone, Zone::Base);
    EXPECT_EQ(s.obj(poro_id).damage, 0);
    EXPECT_TRUE(s.obj(poro_id).exhausted);
    EXPECT_TRUE(s.chain.empty());  // no Deathknell: it didn't die
    EXPECT_EQ(s.player(o).hand.size(), hand + 1);  // Bolt's draw
    EXPECT_TRUE(s.delayed.empty());  // "the next time"
    EXPECT_EQ(s.deaths_this_turn, 0);
    expect_ok(s);
}

TEST(RiftboundReplacement, DelayedReplacementEndsWithTheTurn) {  // 391.3, 317.2.c
    GameState s = started();
    const PlayerId p = s.first_player;
    put(s, p, kPoro, Zone::Base);
    play(s, put(s, p, kRetreat, Zone::Hand));
    both_pass(s);
    EXPECT_EQ(s.delayed.size(), 1u);
    apply(s, {ActionKind::EndTurn});
    EXPECT_TRUE(s.delayed.empty());
}

// ---- Passive abilities and layers ----

TEST(RiftboundLayers, ConditionalPassive) {  // 364.3.a
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId h = put(s, p, kHoarder, Zone::Base);
    ASSERT_GE(s.player(p).hand.size(), 2u);
    EXPECT_EQ(might(s, h), 3);
    while (s.player(p).hand.size() > 1) move_to(s, s.player(p).hand.back(), Zone::Trash, p);
    EXPECT_EQ(might(s, h), 1);
}

TEST(RiftboundLayers, KeywordGrantToOthers) {  // 479.3
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId mine = put(s, p, kKingpin, Zone::Base);
    const ObjId theirs = put(s, o, kKingpin, Zone::Base);
    EXPECT_FALSE(has(s, mine, Keyword::Ganking));
    const ObjId flag = put(s, p, kBanner, Zone::Base);
    EXPECT_TRUE(has(s, mine, Keyword::Ganking));
    EXPECT_FALSE(has(s, theirs, Keyword::Ganking));
    EXPECT_FALSE(has(s, flag, Keyword::Ganking));
    give(s, {.subject = s.obj(mine).ref(mine), .source = {}, .kind = ModKind::Lose, .keyword = Keyword::Ganking});
    EXPECT_FALSE(has(s, mine, Keyword::Ganking));  // a later timestamp loses it (482)
    give(s, {.subject = s.obj(mine).ref(mine), .source = {}, .kind = ModKind::Grant, .keyword = Keyword::Assault,
             .value = 2});
    give(s, {.subject = s.obj(mine).ref(mine), .source = {}, .kind = ModKind::Grant, .keyword = Keyword::Assault,
             .value = 3});
    EXPECT_EQ(traits(s, mine).keywords.get(Keyword::Assault), 5);  // 807.2: values add up
}

TEST(RiftboundLayers, MightyReevaluatesTheLayers) {  // 478.3, 478.4 (Fiora)
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId g = put(s, p, kGiant, Zone::Base);
    EXPECT_EQ(might(s, g), 4);
    EXPECT_FALSE(has(s, g, Keyword::Shield));
    buff(s, g);
    EXPECT_EQ(might(s, g), 6);  // 5 → Mighty → Shield and +1
    EXPECT_TRUE(has(s, g, Keyword::Shield));
    EXPECT_TRUE(mighty(s, g));
    s.obj(g).buffs = 0;
    EXPECT_EQ(might(s, g), 4);  // the buff gone: straight back
    EXPECT_FALSE(has(s, g, Keyword::Shield));
}

TEST(RiftboundLayers, IncreasedToDependsOnIncreases) {  // 479.4.e, 481
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId u = put(s, p, kGiant, Zone::Base);  // 4
    const ObjId small = put(s, p, kPoro, Zone::Base);  // 2
    put(s, p, kAnthem, Zone::Base);
    EXPECT_EQ(might(s, small), 5);
    give_might(s, small, 2, {}, Duration::Turn);
    EXPECT_EQ(might(s, small), 5);  // 2 + 2 = 4, increased to 5
    give_might(s, u, 2, {}, Duration::Turn);
    EXPECT_EQ(might(s, u), 7);  // Discipline first: 6, then nothing to raise; Giant: Mighty, +1
    give_might(s, small, -3, {}, Duration::Turn);
    EXPECT_EQ(might(s, small), 2);  // decreases last
}

TEST(RiftboundLayers, SnapshotMinimum) {  // 479.4.b, 479.4.c
    GameState s = started();
    const PlayerId p = s.first_player;
    const ObjId u = put(s, p, kPoro, Zone::Base);  // 2
    give_might(s, u, -4, {}, Duration::Turn, 1);
    EXPECT_EQ(s.effects.back().value, -1);
    buff(s, u);
    EXPECT_EQ(might(s, u), 2);  // remembered at −1, not re-limited
    give_might(s, u, -10, {}, Duration::Turn);
    double_might(s, u, {}, Duration::Turn);
    EXPECT_EQ(s.effects.back().value, 0);  // doubling a negative Might adds 0
}

TEST(RiftboundLayers, LethalDamageUsesLayeredMight) {  // 142.4.b with 479
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId g = put(s, o, kGiant, Zone::Battlefield);
    buff(s, g);  // 6 Might
    s.cleanup_due = false;
    play(s, put(s, p, kBolt, Zone::Hand));
    both_pass(s);
    EXPECT_EQ(s.obj(g).zone, Zone::Battlefield);
    EXPECT_EQ(s.obj(g).damage, 3);
}

// ---- Costs ----

TEST(RiftboundCosts, DiscountPerCardInTrash) {  // 356.4, 356.4.g, 366.2
    GameState s = started();
    const PlayerId p = s.first_player;  // 2 runes
    const ObjId l = put(s, p, kLurker, Zone::Hand);
    EXPECT_FALSE(offered(s, {ActionKind::Play, l}));  // [5]
    put(s, p, kLurker, Zone::Trash);
    EXPECT_FALSE(offered(s, {ActionKind::Play, l}));  // [3]
    put(s, p, kLurker, Zone::Trash);
    ASSERT_TRUE(offered(s, {ActionKind::Play, l}));  // [1]
    play(s, l);
    EXPECT_EQ(std::count_if(s.player(p).base.begin(), s.player(p).base.end(),
                            [&](ObjId id) { return s.obj(id).card->type == CardType::Rune && s.obj(id).exhausted; }),
              1);
}

TEST(RiftboundCosts, DiscountWithMinimum) {  // 356.4.e
    GameState s = started();
    const PlayerId p = s.first_player;
    put(s, p, kApprentice, Zone::Base);
    put(s, p, kApprentice, Zone::Base);
    const ObjId spell = put(s, p, kPricey, Zone::Hand);
    ChainItem item;
    item.card = spell;
    item.source = s.obj(spell).ref(spell);
    item.controller = p;
    EXPECT_EQ(total_cost(s, item).energy, 1);  // [2] → [1], the second one can't go below its floor
    const ObjId free = put(s, p, kBolt, Zone::Hand);
    item.card = free;
    item.source = s.obj(free).ref(free);
    EXPECT_EQ(total_cost(s, item).energy, 0);  // a floor never raises a cost
}

TEST(RiftboundCosts, OptionalCostDiscard) {  // 355.1.a, 356.2.b, 357.2
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId g = put(s, o, kGiant, Zone::Battlefield);
    const ObjId strike_id = put(s, p, kStrike, Zone::Hand);
    const auto hand = s.player(p).hand.size();
    play(s, strike_id);
    ASSERT_EQ(s.decision.purpose, Purpose::OptionalCost);
    pick(s, strike_id);
    ASSERT_EQ(s.decision.purpose, Purpose::Discard);
    const ObjId gone = legal_actions(s).front().obj;
    pick(s, gone);
    EXPECT_EQ(s.obj(gone).zone, Zone::Trash);
    EXPECT_EQ(s.player(p).hand.size(), hand - 2);
    settle(s);  // the Shadow Legend's discard trigger first, then Strike
    EXPECT_EQ(s.obj(g).zone, Zone::Trash);  // 5 damage
    expect_ok(s);
}

TEST(RiftboundCosts, OptionalCostDeclined) {
    GameState s = started();
    const PlayerId p = s.first_player, o = opponent(p);
    const ObjId g = put(s, o, kGiant, Zone::Battlefield);
    play(s, put(s, p, kStrike, Zone::Hand));
    apply(s, {ActionKind::Done});
    both_pass(s);
    EXPECT_EQ(s.obj(g).damage, 3);
}

TEST(RiftboundCosts, AccelerateOnlyWhenPayable) {  // 805, 355.16
    GameState s = started();
    const PlayerId p = s.first_player;  // 2 runes: [1] + [1] fits
    const ObjId a = put(s, p, kSharkling, Zone::Hand);
    play(s, a);
    ASSERT_EQ(s.decision.purpose, Purpose::OptionalCost);
    pick(s, a);
    EXPECT_FALSE(s.obj(a).exhausted);
    channel(s, p, 1);
    s.cleanup_due = false;
    const ObjId b = put(s, p, kSharkling, Zone::Hand);
    play(s, b);  // 1 rune: no Accelerate offer
    EXPECT_EQ(s.decision.kind, DecisionKind::Priority);
    EXPECT_TRUE(s.obj(b).exhausted);
}

// ---- Random games with every ability kind ----

TEST(RiftboundRandom, AbilityDecks) {  // invariants after every action, outside vanilla mode
    int activated = 0, triggered = 0, choices = 0;
    for (std::uint64_t seed = 0; seed < 20; ++seed) {  // ~0.7 s each under ASan
        const RandomGame g = play_random(ability_deck(), ability_deck(), seed, 100000, false);
        ASSERT_TRUE(g.problem.empty()) << "seed " << seed << ": " << g.problem;
        EXPECT_TRUE(g.winner.has_value());
        activated += g.activations;
        triggered += g.triggers;
        choices += g.choices;
    }
    std::cout << "20 games: " << activated << " activations, " << triggered << " triggers, " << choices << " choices\n";
    EXPECT_GT(activated, 1000);
    EXPECT_GT(triggered, 1000);
    EXPECT_GT(choices, 1000);
}
