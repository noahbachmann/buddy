// Vi Champion Deck (Unleashed, Fury/Order): decks/vi-unleashed.txt. Each card's text above its rules.
#include "cards.hpp"
#include "common.hpp"

namespace buddy::riftbound::cards {

namespace {

// Rengar, Unseen: "[Accelerate] [Assault 2] [Deflect] [Ganking]"
struct Rengar : CardRules {
    Keywords keywords() const override {
        return cards::keywords(
            {{Keyword::Accelerate, 1}, {Keyword::Assault, 2}, {Keyword::Deflect, 1}, {Keyword::Ganking, 1}});
    }
} const rengar;

// Inferna: "[Ambush] [Assault 2]"
struct Inferna : CardRules {
    Keywords keywords() const override { return cards::keywords({{Keyword::Ambush, 1}, {Keyword::Assault, 2}}); }
} const inferna;

// Sharkling: "[Accelerate] [Assault 4]"
struct Sharkling : CardRules {
    Keywords keywords() const override { return cards::keywords({{Keyword::Accelerate, 1}, {Keyword::Assault, 4}}); }
} const sharkling;

// Towering Pairofant: "[Assault] If a unit died this turn, I enter ready."
struct Pairofant : CardRules {
    Keywords keywords() const override { return cards::keywords({{Keyword::Assault, 1}}); }
    bool enters_ready(const GameState& s, ObjId, const ChainItem&) const override { return s.deaths_this_turn > 0; }
} const pairofant;

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

// Loyal Poro: "[Deathknell][>] If I didn't die alone, draw 1. (I wasn't alone if there were other friendly units
// here.)" Alone is noted as it dies (808.1.d.3).
struct LoyalPoro : Deathknell {
    Status resolve(Resolution& r) const override {
        if (!r.item().event.alone) draw(r.s, r.controller(), 1);
        return Status::Done;
    }
} const loyal_poro;

// Carrion Dredger: "[Deathknell][>] Play a 1 Might Bird unit token with [Deflect] to your base."
struct Dredger : Deathknell {
    Status resolve(Resolution& r) const override {
        play_token(r.s, tokens::bird(), r.controller(), played::to_base);
        return Status::Done;
    }
} const dredger;

// Crimson Pigeons: "I have +2 Might while I'm attacking with another unit."
struct Pigeons : CardRules {
    void passive(const GameState& s, ObjId self, ObjId subject, const Traits&, std::vector<Mod>& out) const override {
        const Object& me = s.obj(self);
        if (subject != self || me.designation != Designation::Attacker || me.zone != Zone::Battlefield) return;
        const auto& here = s.battlefields[me.where].here;
        if (std::any_of(here.begin(), here.end(), [&](ObjId id) {
                return id != self && s.obj(id).designation == Designation::Attacker;
            })) {
            out.push_back({ModKind::Might, {}, 2});
        }
    }
} const pigeons;

// Vault Breaker: "[Action] Give a unit [Assault 2] and [Ganking] this turn."
struct VaultBreaker : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) {
            r.give(t, ModKind::Grant, 2, Duration::Turn, Keyword::Assault);
            r.give(t, ModKind::Grant, 1, Duration::Turn, Keyword::Ganking);
        }
        return Status::Done;
    }
} const vault_breaker;

// Square Up: "[Repeat] — Discard 1 Give a unit [Assault 4] this turn."
struct SquareUp : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr Cost kRepeat[] = {discard(1)};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    std::span<const Cost> repeat_costs() const override { return kRepeat; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) r.give(t, ModKind::Grant, 4, Duration::Turn, Keyword::Assault);
        return Status::Done;
    }
} const square_up;

// Right of Conquest: "Draw 1, then draw 1 for each battlefield you or allies control." (Duel: no allies)
struct RightOfConquest : CardRules {
    Status resolve(Resolution& r) const override {
        const PlayerId p = r.controller();
        const auto n = std::count_if(r.s.battlefields.begin(), r.s.battlefields.end(),
                                     [&](const Battlefield& b) { return b.controller == p; });
        draw(r.s, p, 1 + static_cast<int>(n));
        return Status::Done;
    }
} const right_of_conquest;

// Tactical Retreat: "[Reaction] Choose a friendly unit. The next time it would die this turn, heal it, exhaust it,
// and recall it instead." A delayed replacement (390, 367).
struct TacticalRetreat : CardRules {
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
} const tactical_retreat;

// Upstage Comedy: "[Repeat] [2] Ready a unit."
struct UpstageComedy : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr Cost kRepeat[] = {cost(2)};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    std::span<const Cost> repeat_costs() const override { return kRepeat; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) ready(r.s, t);
        return Status::Done;
    }
} const upstage_comedy;

// Soul Harvest: "Kill a unit at a battlefield with 3 Might or less."
struct SoulHarvest : CardRules {
    static bool small(const GameState& s, const ChainItem& item, const Target& t) {
        return filters::unit(s, item, t) && small_at_battlefield(s, t.ref.id);
    }
    static constexpr TargetSpec kTargets[] = {{small}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) kill(r.s, t, by(r));
        return Status::Done;
    }
} const soul_harvest;

// Divining Shells: "[Vision] [Action][>] Kill this, [E]: Give a unit +2 Might this turn."
struct DiviningShells : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.exhaust_self = a.cost.kill_self = true;
        a.targets = kTargets;
        a.timing = Timing::Action;
        return a;
    }()};
    Keywords keywords() const override { return cards::keywords({{Keyword::Vision, 1}}); }
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) give_might(r.s, t, 2, r.source(), Duration::Turn);
        return Status::Done;
    }
} const divining_shells;

// Serrated Dirk: "[Equip] [Fury]". Printed (plan → "Equipment as printed"): Might Bonus +0, effect text [Assault].
struct SerratedDirk : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost = cost(0, 1, domain::fury);
        a.builtin = Builtin::Equip;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Keywords effect_keywords() const override { return cards::keywords({{Keyword::Assault, 1}}); }
} const serrated_dirk;

// Hextech Gauntlets: "[Equip] [3][A]. This ability's Energy cost is reduced by the Might of the unit you choose."
// Printed: Might Bonus +3, no effect text.
struct HextechGauntlets : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.energy = 3;
        a.cost.any_power = 1;
        a.builtin = Builtin::Equip;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    int might_bonus() const override { return 3; }
    void cost_mods(const GameState& s, ObjId self, const ChainItem& item, std::vector<CostMod>& out) const override {
        if (item.kind != ItemKind::Activated || item.source.id != self || item.targets.empty()) return;
        out.push_back({CostMod::Discount, std::max(0, might(s, item.targets.front().ref.id)), 0, 0});
    }
} const hextech_gauntlets;

constexpr Entry kCards[] = {
    {"Rengar, Unseen", &rengar},
    {"Inferna", &inferna},
    {"Sharkling", &sharkling},
    {"Towering Pairofant", &pairofant},
    {"Arena Kingpin", &kingpin},
    {"Loyal Poro", &loyal_poro},
    {"Carrion Dredger", &dredger},
    {"Crimson Pigeons", &pigeons},
    {"Vault Breaker", &vault_breaker},
    {"Square Up", &square_up},
    {"Right of Conquest", &right_of_conquest},
    {"Tactical Retreat", &tactical_retreat},
    {"Upstage Comedy", &upstage_comedy},
    {"Soul Harvest", &soul_harvest},
    {"Divining Shells", &divining_shells},
    {"Serrated Dirk", &serrated_dirk},
    {"Hextech Gauntlets", &hextech_gauntlets},
};

} // namespace

std::span<const Entry> vi_unleashed() { return kCards; }

} // namespace buddy::riftbound::cards
