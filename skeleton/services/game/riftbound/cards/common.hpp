#pragma once
// Helpers the card files share. Internal to cards/.

#include <algorithm>
#include <initializer_list>
#include <utility>

#include "../actions.hpp"
#include "../rules.hpp"
#include "../tokens.hpp"

namespace buddy::riftbound::cards {

inline Keywords keywords(std::initializer_list<std::pair<Keyword, int>> list) {
    Keywords k;
    for (const auto& [kw, n] : list) k.grant(kw, n);
    return k;
}

// Costs as constexpr values (designated initializers that skip fields trip -Wmissing-field-initializers).
constexpr Cost cost(int energy, int power = 0, Domains domains = 0) {
    Cost c;
    c.energy = energy;
    c.power = power;
    c.domains = domains;
    return c;
}
constexpr Cost discard(int n) {
    Cost c;
    c.discard = static_cast<std::uint8_t>(n);
    return c;
}

inline Cause by(Resolution& r) { return {r.source(), r.controller()}; }

// Cards named `name` in a list of ids ("a card with my name in your trash").
inline int count_named(const GameState& s, const std::vector<ObjId>& zone, std::string_view name) {
    return static_cast<int>(
        std::count_if(zone.begin(), zone.end(), [&](ObjId id) { return s.obj(id).card->name == name; }));
}

// "a unit at a battlefield with 3 Might or less" (Soul Harvest, Wind and Ghosts' banish half, Mel).
inline bool small_at_battlefield(const GameState& s, ObjId id) {
    return s.obj(id).zone == Zone::Battlefield && might(s, id) <= 3;
}

// Where an effect can move `id` (447–452; Duel, so 451.2 never bites): any battlefield it isn't at, and its
// controller's base unless it's there. Choose options in the Location decision's shape: Pick obj = battlefield card,
// n = index; base: no_obj, to_base.
inline std::vector<Action> destinations(const GameState& s, ObjId id) {
    const Object& o = s.obj(id);
    std::vector<Action> out;
    for (std::size_t b = 0; b < s.battlefields.size(); ++b) {
        if (o.zone != Zone::Battlefield || o.where != b) {
            out.push_back({ActionKind::Pick, s.battlefields[b].card, static_cast<std::uint8_t>(b)});
        }
    }
    if (o.zone != Zone::Base) out.push_back({ActionKind::Pick, no_obj, to_base});
    return out;
}

// Deathknell (808): the keyword plus a trigger on its own Die event, ability 0.
struct Deathknell : CardRules {
    static constexpr AbilitySpec kAbilities[] = {{AbilityKind::Triggered}};
    std::span<const AbilitySpec> abilities() const override { return kAbilities; }
    Keywords keywords() const override { return cards::keywords({{Keyword::Deathknell, 1}}); }
    void triggers(const GameState&, const Event& e, ObjId self, std::vector<std::uint8_t>& out) const override {
        if (e.kind == EventKind::Die && e.subject.id == self) out.push_back(0);
    }
};

} // namespace buddy::riftbound::cards
