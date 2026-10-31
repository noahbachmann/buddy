#include "hidden.hpp"

#include <algorithm>
#include <stdexcept>

namespace buddy::riftbound {

const Card* hidden_card() {
    static const Card card = [] {
        Card c;
        c.name = "hidden card";
        return c;
    }();
    return &card;
}

GameState view(const GameState& s, PlayerId p) {
    GameState v = s;
    for (Object& o : v.objects) {
        if (!(o.known & 1u << p)) o.card = hidden_card();
    }
    v.rng = Rng{};
    return v;
}

namespace {

// Every card of a deck as new_game creates it.
std::vector<const Card*> all_cards(const Deck& deck) {
    std::vector<const Card*> cards{deck.legend, deck.champion};
    for (const auto* entries : {&deck.main, &deck.runes}) {
        for (const auto& e : *entries) cards.insert(cards.end(), e.count, e.card);
    }
    cards.insert(cards.end(), deck.battlefields.begin(), deck.battlefields.end());
    return cards;
}

} // namespace

GameState determinize(const GameState& view, const Deck& seat0, const Deck& seat1, Rng& rng) {
    GameState s = view;
    for (PlayerId p = 0; p < player_count; ++p) {
        std::vector<const Card*> unseen = all_cards(p == 0 ? seat0 : seat1);
        std::vector<ObjId> runes, others;  // hidden objects
        for (ObjId id = 0; id < s.objects.size(); ++id) {
            const Object& o = s.obj(id);
            if (o.owner != p || o.card->token) continue;
            if (o.card == hidden_card()) {
                (o.zone == Zone::RuneDeck ? runes : others).push_back(id);
                continue;
            }
            const auto it = std::find(unseen.begin(), unseen.end(), o.card);
            if (it == unseen.end()) throw std::logic_error("determinize: " + o.card->name + " is not in the deck");
            unseen.erase(it);
        }
        const auto split = std::stable_partition(unseen.begin(), unseen.end(),
                                                  [](const Card* c) { return c->type == CardType::Rune; });
        std::vector<const Card*> rune_cards(unseen.begin(), split), other_cards(split, unseen.end());
        if (rune_cards.size() != runes.size() || other_cards.size() != others.size()) {
            throw std::logic_error("determinize: player " + std::to_string(p) + " has " +
                                   std::to_string(runes.size() + others.size()) + " hidden cards, the deck " +
                                   std::to_string(unseen.size()));
        }
        rng.shuffle(rune_cards);
        rng.shuffle(other_cards);
        for (std::size_t i = 0; i < runes.size(); ++i) s.obj(runes[i]).card = rune_cards[i];
        for (std::size_t i = 0; i < others.size(); ++i) s.obj(others[i]).card = other_cards[i];
    }
    s.rng = Rng(rng.next());
    return s;
}

} // namespace buddy::riftbound
