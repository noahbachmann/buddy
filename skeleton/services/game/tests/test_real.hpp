#pragma once
// Tests of the decks' real cards (5.3): cards from the fixture (stats, no text) with their rules from cards/, a test
// frame around them, and a random-game deck of everything a deck file registers so far.

#include <span>
#include <stdexcept>
#include <string>

#include "riftbound/actions.hpp"
#include "riftbound/card_db.hpp"
#include "riftbound/cards/cards.hpp"
#include "test_play.hpp"

namespace buddy::riftbound::test {

inline const CardDb& fixture() {
    static const CardDb db = CardDb::load(std::string(BUDDY_SOURCE_DIR) + "/services/game/tests/data/cards.json");
    return db;
}

// A deck card by name; it must have rules.
inline const Card* real(std::string_view name) {
    const Card* c = fixture().find_name(name);
    if (c == nullptr || c->rules == nullptr) throw std::logic_error("no implemented card " + std::string(name));
    return c;
}

// "Deal 9 to a unit.", free: kills whatever a test needs dead through a game action.
struct ZapRules : CardRules {
    static constexpr TargetSpec kTargets[] = {{filters::unit}};
    std::span<const TargetSpec> targets() const override { return kTargets; }
    Status resolve(Resolution& r) const override {
        if (const ObjId t = r.target(); t != no_obj) deal(r.s, t, 9, by(r));
        return Status::Done;
    }
};
inline const ZapRules zap_rules;
inline const Card* zap() {
    static const Card* c = make("Zap", CardType::Spell, 0, 0, zap_rules, Timing::Reaction);
    return c;
}

// deck_of's frame with these cards (3 each), Zap, and 6 + 6 runes of the deck's two domains.
inline Deck real_deck(const std::string& prefix, std::initializer_list<std::string_view> names, Domains second) {
    Deck d = deck_of(prefix, {zap()});
    for (const auto name : names) d.main.push_back({real(name), 3});
    d.runes.clear();
    d.runes.push_back({fixture().find_name("Fury Rune"), 6});
    d.runes.push_back({fixture().find_name(second == domain::order ? "Order Rune" : "Chaos Rune"), 6});
    return d;
}

// Every card a deck file registers: legend and battlefields into the frame, the rest 3× into the main deck (plain
// units fill it up to 13 lines). For random games.
inline Deck registered_deck(const std::string& prefix, std::span<const cards::Entry> entries, Domains second) {
    Deck d = real_deck(prefix, {}, second);
    d.main.clear();
    std::vector<const Card*> fields;
    for (const auto& e : entries) {
        const Card* c = real(e.name);
        if (c->type == CardType::Legend) d.legend = c;
        else if (c->type == CardType::Battlefield) fields.push_back(c);
        else d.main.push_back({c, 3});
    }
    for (std::size_t i = 0; i < fields.size() && i < d.battlefields.size(); ++i) d.battlefields[i] = fields[i];
    const Deck filler = ruled(simple_deck(prefix));
    for (std::size_t i = 0; d.main.size() < 13; ++i) d.main.push_back(filler.main[i]);
    return d;
}

// Energy and Power of `domains` (one each) into p's pool: pays what the 2 runes of turn 1 can't.
inline void fund(GameState& s, PlayerId p, int energy, std::initializer_list<Domains> power = {}) {
    s.player(p).pool.energy = static_cast<std::int16_t>(s.player(p).pool.energy + energy);
    for (const Domains d : power) ++s.player(p).pool.power[domain_index(d)];
}

// Kills `id` with Zap from p's side, then lets everything resolve (first options).
inline void zap_kill(GameState& s, PlayerId p, ObjId id) {
    play(s, put(s, p, zap(), Zone::Hand));
    if (s.decision.kind == DecisionKind::Choose) pick(s, id);
    settle(s);
}

// A copy of `card` at battlefield b, which p controls (no contest, no showdown).
inline ObjId at_field(GameState& s, PlayerId p, const Card* card, std::uint8_t b) {
    const ObjId id = put(s, p, card, Zone::Battlefield, b);
    s.battlefields[b].controller = p;
    return id;
}

// Answers an OptionalCost decision: pay (Pick) or not (Done).
inline void optional_cost(GameState& s, bool pay) {
    ASSERT_EQ(s.decision.purpose, Purpose::OptionalCost);
    apply(s, pay ? legal_actions(s).front() : Action{ActionKind::Done});
}

// A copy of `card` in p's hand: one already there, else one from the deck (put() may take one from the trash).
inline ObjId in_hand(GameState& s, PlayerId p, const Card* card) {
    for (const ObjId id : s.player(p).hand) {
        if (s.obj(id).card == card) return id;
    }
    for (const ObjId id : s.player(p).main_deck) {
        if (s.obj(id).card != card) continue;
        move_to(s, id, Zone::Hand, p);
        return id;
    }
    throw std::logic_error("no copy of " + card->name + " in hand or deck");
}

// A Standard Move of `ids` to `where` (a battlefield or to_base).
inline void move_units(GameState& s, std::uint8_t where, std::vector<ObjId> ids) {
    apply(s, {ActionKind::Move, where == to_base ? no_obj : s.battlefields[where].card, where});
    std::sort(ids.begin(), ids.end());
    for (const ObjId id : ids) {
        if (s.decision.purpose == Purpose::Move) apply(s, {ActionKind::Pick, id});
    }
    if (s.decision.purpose == Purpose::Move) apply(s, {ActionKind::Done});
}

// A copy of `card` in p's base, ready to move.
inline ObjId ready_in_base(GameState& s, PlayerId p, const Card* card) {
    const ObjId id = put(s, p, card, Zone::Base);
    s.obj(id).exhausted = false;
    return id;
}

// Passes until no showdown is open (a move to an empty battlefield starts one) and the turn player may act.
inline void close_showdown(GameState& s) {
    settle(s);
    while (s.showdown) {
        pass(s);
        settle(s);
    }
}

inline int count_on_board(const GameState& s, PlayerId p, std::string_view name) {
    int n = 0;
    for (const Object& o : s.objects) n += on_board(o.zone) && o.controller == p && o.card->name == name;
    return n;
}

} // namespace buddy::riftbound::test
