#pragma once
// Cards and decks defined in code, without cards.json: rules tests name exactly the stats they need. Text-free, so
// games with them run in vanilla mode. Card pointers stay valid for the whole test run (std::deque).

#include <deque>
#include <string>

#include "riftbound/deck.hpp"

namespace buddy::riftbound::test {

inline const Card* card(Card c) {
    static std::deque<Card> cards;
    return &cards.emplace_back(std::move(c));
}

inline const Card* card(std::string name, CardType type, Domains domains = domain::fury) {
    Card c;
    c.name = std::move(name);
    c.type = type;
    c.domains = domains;
    return card(std::move(c));
}

inline const Card* unit(std::string name, int energy, int might, bool champion = false) {
    Card c;
    c.name = std::move(name);
    c.domains = domain::fury;
    c.energy = energy;
    c.might = might;
    c.champion = champion;
    return card(std::move(c));
}

inline const Card* spell(std::string name, int energy, Timing timing = Timing::Normal) {
    Card c;
    c.name = std::move(name);
    c.type = CardType::Spell;
    c.domains = domain::fury;
    c.energy = energy;
    c.power = 0;
    c.timing = timing;
    return card(std::move(c));
}

// Legal by 103: legend, chosen champion, 13 × 3 units, 12 runes, 3 battlefields. Names start with `prefix`, so the
// two seats' cards tell apart: "A Unit 7", "B Field 2".
inline Deck simple_deck(const std::string& prefix) {
    Deck d;
    d.legend = card(prefix + " Legend", CardType::Legend);
    d.champion = unit(prefix + " Champion", 3, 3, true);
    for (int i = 1; i <= 13; ++i) d.main.push_back({unit(prefix + " Unit " + std::to_string(i), i % 6, 1 + i % 5), 3});
    d.runes.push_back({card("Fury Rune", CardType::Rune), 12});
    for (int i = 1; i <= 3; ++i) {
        d.battlefields.push_back(card(prefix + " Field " + std::to_string(i), CardType::Battlefield, 0));
    }
    return d;
}

// simple_deck with three unit lines swapped for spells: a Normal, an Action and a Reaction one, 3 copies each.
inline Deck spell_deck(const std::string& prefix) {
    Deck d = simple_deck(prefix);
    d.main[1] = {spell(prefix + " Spell", 1), 3};
    d.main[2] = {spell(prefix + " Action", 2, Timing::Action), 3};
    d.main[3] = {spell(prefix + " Reaction", 1, Timing::Reaction), 3};
    return d;
}

} // namespace buddy::riftbound::test
