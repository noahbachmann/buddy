// Zed deck of the Vendetta Showdown Decks (Fury/Chaos): decks/zed-vendetta.txt. Each card's text above its rules.
#include "cards.hpp"
#include "common.hpp"

namespace buddy::riftbound::cards {

namespace {

// "[Empower] <cost>" as ability 0 (827).
constexpr AbilitySpec empower_for(Cost cost) {
    AbilitySpec a;
    a.cost = cost;
    a.builtin = Builtin::Empower;
    return a;
}

// Shadow Assassin: "I enter ready if you have a card with my name in your trash."
struct ShadowAssassin : CardRules {
    bool enters_ready(const GameState& s, ObjId self, const ChainItem& played) const override {
        return count_named(s, s.player(played.controller).trash, s.obj(self).card->name) > 0;
    }
} const shadow_assassin;

// Shadowblade Lurker: "I cost [2] less for each card with my name in your trash."
struct Lurker : CardRules {
    void cost_mods(const GameState& s, ObjId self, const ChainItem& item, std::vector<CostMod>& out) const override {
        if (item.kind != ItemKind::Card || item.card != self) return;
        const int n = count_named(s, s.player(item.controller).trash, s.obj(self).card->name);
        if (n > 0) out.push_back({CostMod::Discount, 2 * n, 0, 0});
    }
} const lurker;

// Kinkou Lifeblade: "[Empower] [2] [Empowered][>] I have +1 Might and [Ganking]."
struct Kinkou : CardRules {
    static constexpr AbilitySpec kAbilities[] = {empower_for(cost(2))};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void passive(const GameState& s, ObjId self, ObjId subject, const Traits&, std::vector<Mod>& out) const override {
        if (subject != self || !s.obj(self).empowered) return;
        out.push_back({ModKind::Might, {}, 1});
        out.push_back({ModKind::Grant, Keyword::Ganking, 1});
    }
} const kinkou;

// Shadow Fiend: "[Empower] [2][Fury] [Empowered][>] I have [Assault 3]."
struct ShadowFiend : CardRules {
    static constexpr AbilitySpec kAbilities[] = {empower_for(cost(2, 1, domain::fury))};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void passive(const GameState& s, ObjId self, ObjId subject, const Traits&, std::vector<Mod>& out) const override {
        if (subject == self && s.obj(self).empowered) out.push_back({ModKind::Grant, Keyword::Assault, 3});
    }
} const shadow_fiend;

// Punching Poro: "[Empower] — Discard 1 [Empowered][>] I have +1 Might."
struct PunchingPoro : CardRules {
    static constexpr AbilitySpec kAbilities[] = {empower_for(discard(1))};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void passive(const GameState& s, ObjId self, ObjId subject, const Traits&, std::vector<Mod>& out) const override {
        if (subject == self && s.obj(self).empowered) out.push_back({ModKind::Might, {}, 1});
    }
} const punching_poro;

// Ruthless Strike: "[Action] As an additional cost to play this, you may discard 1. Deal 3 to a unit at a
// battlefield. If you paid the additional cost, deal 5 to it instead."
struct RuthlessStrike : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit_at_battlefield}};
    static constexpr Cost kOptional[] = {discard(1)};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    std::span<const Cost> optional_costs() const override { return kOptional; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) deal(r.s, t, (r.item().paid & 1) ? 5 : 3, by(r));
        return Status::Done;
    }
} const ruthless_strike;

// Perfect Execution: "Ready a unit and give it [Assault 3] this turn. [Flow] [3][Fury]"
struct PerfectExecution : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    std::optional<Cost> flow_cost() const override { return cost(3, 1, domain::fury); }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) {
            ready(r.s, t);
            r.give(t, ModKind::Grant, 3, Duration::Turn, Keyword::Assault);
        }
        return Status::Done;
    }
} const perfect_execution;

// Twilight Step: "Move a unit with 3 Might or less. [Flow] [4][Chaos]". The text names no destination: the
// controller picks one as it resolves (any other location, 451.1). ⚠️ ruling (plan → 5.3 rulings).
struct TwilightStep : CardRules {
    static bool small(const GameState& s, const ChainItem& item, const Target& t) {
        return filters::unit(s, item, t) && might(s, t.ref.id) <= 3;
    }
    static constexpr TargetSpec kTargets[] = {{small}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    std::optional<Cost> flow_cost() const override { return cost(4, 1, domain::chaos); }
    Status resolve(Resolution& r) const override {
        const ObjId t = r.target();
        if (t == no_obj) return Status::Done;
        if (r.step() == 0) {
            r.ask(r.controller(), destinations(r.s, t), 1, 1);
            return r.wait(1);
        }
        move(r.s, {t}, r.answer().front().n, by(r));
        return Status::Done;
    }
} const twilight_step;

// Wind and Ghosts: "[Action] Choose a unit at a battlefield. If it has 3 Might or less, banish it. Otherwise, return
// it to its owner's hand."
struct WindAndGhosts : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit_at_battlefield}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    Status resolve(Resolution& r) const override {
        const ObjId t = r.target();
        if (t == no_obj) return Status::Done;
        if (might(r.s, t) <= 3) banish(r.s, t, by(r));
        else return_to_hand(r.s, t);
        return Status::Done;
    }
} const wind_and_ghosts;

// Death Mark: "[Burn 3]. Play a 0 Might Shadow Clone unit token. [Flow] [1][A][A]". The token's place isn't
// stated: a location choice like any unit's (184.2).
struct DeathMark : CardRules {
    std::optional<Cost> flow_cost() const override {
        Cost c = cost(1);
        c.any_power = 2;
        return c;
    }
    Status resolve(Resolution& r) const override {
        burn(r.s, r.controller(), 3);
        play_token(r.s, tokens::shadow_clone(), r.controller());
        return Status::Done;
    }
} const death_mark;

constexpr Entry kCards[] = {
    {"Shadow Assassin", &shadow_assassin},
    {"Shadowblade Lurker", &lurker},
    {"Kinkou Lifeblade", &kinkou},
    {"Shadow Fiend", &shadow_fiend},
    {"Punching Poro", &punching_poro},
    {"Ruthless Strike", &ruthless_strike},
    {"Perfect Execution", &perfect_execution},
    {"Twilight Step", &twilight_step},
    {"Wind and Ghosts", &wind_and_ghosts},
    {"Death Mark", &death_mark},
};

} // namespace

std::span<const Entry> zed_vendetta() { return kCards; }

} // namespace buddy::riftbound::cards
