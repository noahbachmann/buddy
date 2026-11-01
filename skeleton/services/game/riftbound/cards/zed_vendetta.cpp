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

// Zed, From the Shadows: "You may discard 1 as an additional cost to play me. When you play me, if you paid the
// additional cost, play a 0 Might Shadow Clone unit token." The token's place: a location choice (184.2).
struct ZedFromTheShadows : CardRules {
    static constexpr Cost kOptional[] = {discard(1)};
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const Cost> optional_costs() const override { return kOptional; }
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (played_me(e, self) && (e.amount & 1)) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        play_token(r.s, tokens::shadow_clone(), r.controller());
        return Status::Done;
    }
} const zed_from_the_shadows;

// Zed, Without a Sound: "When I conquer, play a 0 Might Shadow Clone unit token to your base. [Action][>] [1][Chaos]:
// Move me and a Shadow Clone you control to each other's locations."
struct ZedWithoutASound : CardRules {
    static bool my_clone(const GameState& s, const ChainItem& item, const Target& t) {
        return filters::friendly_unit(s, item, t) && s.obj(t.ref.id).card == &tokens::shadow_clone();
    }
    static constexpr TargetSpec kTargets[] = {{my_clone}};
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}, [] {
        AbilitySpec a;
        a.cost = cost(1, 1, domain::chaos);
        a.targets = kTargets;
        a.timing = Timing::Action;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (conquered_with(s, e, self)) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        if (r.ability() == 0) {
            play_token(r.s, tokens::shadow_clone(), r.controller(), played::to_base);
            return Status::Done;
        }
        const ObjId me = r.self(), clone = r.target();
        if (me == no_obj || clone == no_obj) return Status::Done;
        const auto mine = battlefield_of(r.s, me).value_or(to_base), theirs = battlefield_of(r.s, clone).value_or(to_base);
        if (mine == theirs) return Status::Done;  // ⚠️ same location: no move at all (plan → 5.3 rulings)
        move(r.s, {me}, theirs, by(r));
        move(r.s, {clone}, mine, by(r));
        return Status::Done;
    }
} const zed_without_a_sound;

// Morgana, Vindictive: "[Ambush] When you play me, deal damage to a unit equal to the damage marked on it."
struct Morgana : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.targets = kTargets;
        return a;
    }()};
    Keywords keywords() const override { return cards::keywords({{Keyword::Ambush, 1}}); }
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (played_me(e, self)) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {  // the damage as it resolves
        if (const ObjId t = r.target(); t != no_obj && r.s.obj(t).damage > 0) deal(r.s, t, r.s.obj(t).damage, by(r));
        return Status::Done;
    }
} const morgana;

// Mel, Defiant Soul: "[Empower] — Discard a spell. When I become [Empowered], banish an enemy unit at a battlefield
// with 3 Might or less."
struct Mel : CardRules {
    static bool small_enemy(const GameState& s, const ChainItem& item, const Target& t) {
        return filters::enemy_unit(s, item, t) && small_at_battlefield(s, t.ref.id);
    }
    static constexpr TargetSpec kTargets[] = {{small_enemy}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.cost = discard(1);
        a.cost.discard_spell = true;
        a.builtin = Builtin::Empower;
        return a;
    }(), [] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.targets = kTargets;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Empowered && e.subject.id == self) out.push_back(1);
    }
    Status resolve(Resolution& r) const override {  // ability 0 is the engine's (Builtin::Empower)
        if (const ObjId t = r.target(); t != no_obj) banish(r.s, t, by(r));
        return Status::Done;
    }
} const mel;

// Blade Twirler: "The first time I move each turn, choose a player. They [Burn 1]."
struct BladeTwirler : CardRules {
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
        a.per_turn = 1;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Moved && e.subject.id == self) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        if (const auto p = r.target_player()) burn(r.s, *p, 1);
        return Status::Done;
    }
} const blade_twirler;

// Shadow Order Disciple: "When I move, you may [Burn 1] to give me +1 Might this turn."
struct ShadowOrderDisciple : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.cost.burn = 1;
        a.may = true;
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
} const shadow_order_disciple;

// Traveling Merchant: "When I move, discard 1, then draw 1."
struct TravelingMerchant : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Moved && e.subject.id == self) out.push_back(0);
    }
    Status resolve(Resolution& r) const override { return loot(r, r.controller()); }
} const traveling_merchant;

// Gust Monk: "You may pay [1] as an additional cost to play me. When you play me, if you paid the additional cost,
// banish a card from any trash to give a unit [Assault 2] this turn." The card is picked as it resolves; none to
// banish: no Assault (383.3.b-style "do X to").
struct GustMonk : CardRules {
    static constexpr Cost kOptional[] = {cost(1)};
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.targets = kTargets;
        return a;
    }()};
    std::span<const Cost> optional_costs() const override { return kOptional; }
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (played_me(e, self) && (e.amount & 1)) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        if (r.step() == 0) {
            std::vector<Action> cards;
            for (PlayerId p = 0; p < player_count; ++p) {
                for (const ObjId id : r.s.player(p).trash) cards.push_back({ActionKind::Pick, id});
            }
            std::sort(cards.begin(), cards.end(), [](const Action& a, const Action& b) { return a.obj < b.obj; });
            if (cards.empty()) return Status::Done;
            r.ask(r.controller(), std::move(cards), 1, 1);
            return r.wait(1);
        }
        banish(r.s, r.answer().front().obj, by(r));
        if (const ObjId t = r.target(); t != no_obj) r.give(t, ModKind::Grant, 2, Duration::Turn, Keyword::Assault);
        return Status::Done;
    }
} const gust_monk;

// Twilight Reveler: "When I attack, ready another friendly unit."
struct TwilightReveler : CardRules {
    static bool other_friendly(const GameState& s, const ChainItem& item, const Target& t) {
        return filters::friendly_unit(s, item, t) && t.ref.id != item.source.id;
    }
    static constexpr TargetSpec kTargets[] = {{other_friendly}};
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.targets = kTargets;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Attack && e.subject.id == self) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) ready(r.s, t);
        return Status::Done;
    }
} const twilight_reveler;

// Ravenbloom Prefect: "When an opponent plays a gear, you may banish me to banish it."
struct RavenbloomPrefect : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.cost.banish_self = true;
        a.may = true;
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
        if (r.s.alive(gear) && on_board(r.s.obj(gear.id).zone)) banish(r.s, gear.id, by(r));
        return Status::Done;
    }
} const ravenbloom_prefect;

// Zed, Master of Shadows (legend): "When you banish a card you own, empower me. [Action][>] Disempower me, [E]:
// Discard 1, then draw 1."
struct ZedMasterOfShadows : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}, [] {
        AbilitySpec a;
        a.cost.disempower_self = a.cost.exhaust_self = true;
        a.timing = Timing::Action;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState& s, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        const PlayerId me = s.obj(self).controller;
        if (e.kind == EventKind::Banished && e.player == me && s.obj(e.subject.id).owner == me) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        if (r.ability() == 1) return loot(r, r.controller());
        if (const ObjId me = r.self(); me != no_obj) empower(r.s, me);
        return Status::Done;
    }
} const zed_master_of_shadows;

// Zaun Warrens: "When you conquer here, discard 1, then draw 1."
struct ZaunWarrens : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.event_player = true;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Conquer && e.subject.id == self) out.push_back(0);
    }
    Status resolve(Resolution& r) const override { return loot(r, r.controller()); }
} const zaun_warrens;

// Shadow Temple: "When you hold here, [Burn 3]."
struct ShadowTemple : CardRules {
    static constexpr AbilitySpec kAbilities[] = {[] {
        AbilitySpec a;
        a.kind = AbilityKind::Triggered;
        a.event_player = true;
        return a;
    }()};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Hold && e.subject.id == self) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        burn(r.s, r.controller(), 3);
        return Status::Done;
    }
} const shadow_temple;

// Threshold of the Gray: "When combat starts here, the attacker and defender each [Add] [1]." No choices: the
// battlefield's controller (or the turn player) controls it (190.6.a–b).
struct ThresholdOfTheGray : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::CombatStart && e.subject.id == self) out.push_back(0);
    }
    Status resolve(Resolution& r) const override {
        const PlayerId attacker = r.item().event.player;
        add(r.s, attacker, 1);
        add(r.s, opponent(attacker), 1);
        return Status::Done;
    }
} const threshold_of_the_gray;

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
    {"Zed, From the Shadows", &zed_from_the_shadows},
    {"Zed, Without a Sound", &zed_without_a_sound},
    {"Morgana, Vindictive", &morgana},
    {"Mel, Defiant Soul", &mel},
    {"Blade Twirler", &blade_twirler},
    {"Shadow Order Disciple", &shadow_order_disciple},
    {"Traveling Merchant", &traveling_merchant},
    {"Gust Monk", &gust_monk},
    {"Twilight Reveler", &twilight_reveler},
    {"Ravenbloom Prefect", &ravenbloom_prefect},
    {"Zed, Master of Shadows", &zed_master_of_shadows},
    {"Zaun Warrens", &zaun_warrens},
    {"Shadow Temple", &shadow_temple},
    {"Threshold of the Gray", &threshold_of_the_gray},
};

} // namespace

std::span<const Entry> zed_vendetta() { return kCards; }

} // namespace buddy::riftbound::cards
