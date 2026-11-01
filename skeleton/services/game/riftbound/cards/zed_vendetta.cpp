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

constexpr Entry kCards[] = {
    {"Shadow Assassin", &shadow_assassin},
    {"Shadowblade Lurker", &lurker},
    {"Kinkou Lifeblade", &kinkou},
    {"Shadow Fiend", &shadow_fiend},
    {"Punching Poro", &punching_poro},
};

} // namespace

std::span<const Entry> zed_vendetta() { return kCards; }

} // namespace buddy::riftbound::cards
