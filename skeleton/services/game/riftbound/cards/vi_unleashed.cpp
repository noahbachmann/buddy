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

// Vi, Hotheaded: "[Deflect] [2][Fury]: Double my Might this turn."
struct ViHotheaded : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Activated, cost(2, 1, domain::fury)}};
    Keywords keywords() const override { return cards::keywords({{Keyword::Deflect, 1}}); }
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Status resolve(Resolution& r) const override {
        if (const ObjId me = r.self(); me != no_obj) double_might(r.s, me, r.source(), Duration::Turn);
        return Status::Done;
    }
} const vi_hotheaded;

// Vi, Destructive: "[Ganking] Recycle 1 from your trash: Give me +1 Might this turn."
struct ViDestructive : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost.recycle_trash = 1;
        return a;
    }()};
    Keywords keywords() const override { return cards::keywords({{Keyword::Ganking, 1}}); }
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Status resolve(Resolution& r) const override {
        if (const ObjId me = r.self(); me != no_obj) give_might(r.s, me, 1, r.source(), Duration::Turn);
        return Status::Done;
    }
} const vi_destructive;

// Vi, Peacekeeper: "[Ambush] When I attack, [Stun] an enemy unit here."
struct ViPeacekeeper : CardRules {
    static bool enemy_here(const GameState& s, const ChainItem& item, const Target& t) {
        const auto here = battlefield_of(s, item.source.id);
        return filters::enemy_unit(s, item, t) && here && battlefield_of(s, t.ref.id) == here;
    }
    static constexpr TargetSpec kTargets[] = {{enemy_here}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.targets = kTargets;
        return a;
    }()};
    Keywords keywords() const override { return cards::keywords({{Keyword::Ambush, 1}}); }
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Attack && e.subject.id == self) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) stun(r.s, t);
        return Status::Done;
    }
} const vi_peacekeeper;

// Xerath, Freed: "[Fury], [E]: Deal 3 to a unit. Use this ability only while I'm at a battlefield."
struct Xerath : CardRules {
    static bool at_battlefield(const GameState& s, ObjId self) { return battlefield_of(s, self).has_value(); }
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost = cost(0, 1, domain::fury);
        a.cost.exhaust_self = true;
        a.targets = kTargets;
        a.usable = at_battlefield;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) deal(r.s, t, 3, by(r));
        return Status::Done;
    }
} const xerath;

// Lord Broadmane: "[Ambush] When you play me, give your other units here [Assault] this turn."
struct LordBroadmane : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    Keywords keywords() const override { return cards::keywords({{Keyword::Ambush, 1}}); }
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (played_me(e, self)) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        const Place here = r.item().event.place;  // where I entered
        const ObjId me = r.item().event.subject.id;
        for (ObjId id = 0; id < r.s.objects.size(); ++id) {
            const Object& o = r.s.obj(id);
            if (id == me || o.card->type != CardType::Unit || o.controller != r.controller() || o.zone != here.zone ||
                (here.zone == Zone::Battlefield && o.where != here.where)) {
                continue;
            }
            r.give(id, ModKind::Grant, 1, Duration::Turn, Keyword::Assault);
        }
        return Status::Done;
    }
} const lord_broadmane;

// Yeti Brawler: "When I conquer, if you assigned 3 or more excess damage, play two Gold gear tokens exhausted."
struct YetiBrawler : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (conquered_with(s, e, self) && e.amount >= 3) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        for (int i = 0; i < 2; ++i) play_token(r.s, tokens::gold(), r.controller(), played::to_base | played::exhausted);
        return Status::Done;
    }
} const yeti_brawler;

// Mageseeker Investigator: "Opponents must pay [A] for each unit beyond the first to move multiple units to my
// battlefield at the same time." Standard Moves only (the only way to move several at once); "my battlefield" = the
// one I'm at.
struct Mageseeker : CardRules {
    Cost move_tax(const GameState& s, ObjId self, PlayerId mover, std::uint8_t where, int units) const override {
        Cost c;
        if (mover != s.obj(self).controller && battlefield_of(s, self) == where && units > 1) c.any_power = units - 1;
        return c;
    }
} const mageseeker;

// Vi, Piltover Enforcer (legend): "When you conquer, if you assigned 3 or more excess damage, you may exhaust me to
// ready a unit."
struct ViPiltoverEnforcer : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.cost.exhaust_self = true;
        a.may = true;
        a.targets = kTargets;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Conquer && e.player == s.obj(self).controller && e.amount >= 3) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) ready(r.s, t);
        return Status::Done;
    }
} const vi_piltover_enforcer;

// Trapping Grounds: "When you conquer here, if you assigned 3 or more excess damage, play a 1 Might Bird unit token
// with [Deflect]." "You" = whoever conquered, who controls it now (190.6.d). The token's place: a location choice.
struct TrappingGrounds : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.event_player = true;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Conquer && e.subject.id == self && e.amount >= 3) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        play_token(r.s, tokens::bird(), r.controller());
        return Status::Done;
    }
} const trapping_grounds;

// Star Spring: "The first time a player plays a non-token unit here each turn, they may move another unit they
// control here to its base." Once per turn for all players (Dev's ruling); the unit is a target of that player.
struct StarSpring : CardRules {
    static bool other_here(const GameState& s, const ChainItem& item, const Target& t) {
        const Place here = item.event.place;
        return filters::friendly_unit(s, item, t) && t.ref.id != item.event.subject.id &&
               battlefield_of(s, t.ref.id) == std::optional<std::uint8_t>(here.where);
    }
    static constexpr TargetSpec kTargets[] = {{other_here}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.may = true;
        a.per_turn = 1;
        a.targets = kTargets;
        a.event_player = true;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind != EventKind::Played) return;
        const Object& unit = s.obj(e.subject.id);
        if (unit.card->type == CardType::Unit && !unit.card->token &&
            e.place.zone == Zone::Battlefield && e.place.where == s.obj(self).where) {
            out.push_back(0);
        }
    }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) move(r.s, {t}, to_base, by(r));
        return Status::Done;
    }
} const star_spring;

// Valley of Idols: "When a player plays a unit here, they may pay [1] to [Buff] it."
struct ValleyOfIdols : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.cost = cost(1);
        a.may = true;
        a.event_player = true;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Played && s.obj(e.subject.id).card->type == CardType::Unit &&
            e.place.zone == Zone::Battlefield && e.place.where == s.obj(self).where) {
            out.push_back(0);
        }
    }
    Status resolve(Resolution& r) const override {
        const ObjRef unit = r.item().event.subject;
        if (r.s.alive(unit) && on_board(r.s.obj(unit.id).zone)) buff(r.s, unit.id);
        return Status::Done;
    }
} const valley_of_idols;

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
    {"Vi, Hotheaded", &vi_hotheaded},
    {"Vi, Destructive", &vi_destructive},
    {"Vi, Peacekeeper", &vi_peacekeeper},
    {"Xerath, Freed", &xerath},
    {"Lord Broadmane", &lord_broadmane},
    {"Yeti Brawler", &yeti_brawler},
    {"Mageseeker Investigator", &mageseeker},
    {"Vi, Piltover Enforcer", &vi_piltover_enforcer},
    {"Trapping Grounds", &trapping_grounds},
    {"Star Spring", &star_spring},
    {"Valley of Idols", &valley_of_idols},
};

} // namespace

std::span<const Entry> vi_unleashed() { return kCards; }

} // namespace buddy::riftbound::cards
