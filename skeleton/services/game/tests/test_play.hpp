#pragma once
// Helpers for tests that drive a game through legal actions: a started game over a test deck, cards put where a
// test needs them, the usual answers. Shared by test_actions.cpp and test_keywords.cpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>

#include "riftbound/engine.hpp"
#include "test_cards.hpp"

namespace buddy::riftbound::test {

// A Fury card with these stats and rules.
inline const Card* make(std::string name, CardType type, int energy, int might, const CardRules& rules,
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
    return card(std::move(c));
}

// Rules with nothing but printed keywords.
struct Keyworded : CardRules {
    Keywords k;
    explicit Keyworded(Keyword kw, int n = 1) { k.grant(kw, n); }
    Keywords keywords() const override { return k; }
};

// simple_deck's frame (plain legend, champion, battlefields, Fury runes) with 3 of each card as the main deck.
inline Deck deck_of(const std::string& prefix, std::initializer_list<const Card*> cards) {
    Deck d = ruled(simple_deck(prefix));
    d.main.clear();
    for (const Card* c : cards) d.main.push_back({c, 3});
    return d;
}

// Both keep: turn 1's main-phase priority for the first player, who has 2 ready runes.
inline GameState started(const Deck& deck, std::uint64_t seed = 5) {
    GameState s = new_game(deck, deck, {.seed = seed});
    apply(s, {ActionKind::Done});
    apply(s, {ActionKind::Done});
    return s;
}

// A copy of `card` of player p out of the deck (or else the hand) into `zone`. Board zones: p controls it.
inline ObjId put(GameState& s, PlayerId p, const Card* card, Zone zone, std::uint8_t where = 0) {
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

inline bool offered(const GameState& s, const Action& a) {
    const auto actions = legal_actions(s);
    return std::find(actions.begin(), actions.end(), a) != actions.end();
}

inline void play(GameState& s, ObjId id) { apply(s, {ActionKind::Play, id}); }
inline void pass(GameState& s) { apply(s, {ActionKind::Pass}); }
inline void pick(GameState& s, ObjId id) { apply(s, {ActionKind::Pick, id}); }
inline void done(GameState& s) { apply(s, {ActionKind::Done}); }
inline void both_pass(GameState& s) {
    pass(s);
    pass(s);
}

// Answers everything with the first option and passes until the chain is empty and the turn player may act.
inline void settle(GameState& s) {
    while (!s.chain.empty() || s.decision.kind != DecisionKind::Priority) {
        if (s.decision.kind == DecisionKind::Choose) apply(s, legal_actions(s).front());
        else pass(s);
    }
}

inline void expect_ok(const GameState& s) {
    const auto problems = check_invariants(s);
    EXPECT_TRUE(problems.empty()) << problems.front();
}

inline Cause by(Resolution& r) { return {r.source(), r.controller()}; }

} // namespace buddy::riftbound::test
