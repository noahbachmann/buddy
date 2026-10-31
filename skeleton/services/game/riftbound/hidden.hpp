#pragma once
// Hidden information (Core Rules 108, 128): what one player may know of a game, and states consistent with it. The
// GameState is the referee's full truth; the AI (5.4) and the panel (5.6) work from a view. Which player knows which
// card is tracked in Object::known (move_to, recycle, predict).

#include "deck.hpp"
#include "rng.hpp"
#include "state.hpp"

namespace buddy::riftbound {

// Placeholder for a card the viewer doesn't know: "hidden card", a Unit without stats or rules.
const Card* hidden_card();

// The game as player p knows it: every card p doesn't know (the opponent's hand, both decks' order, facedown cards
// of the opponent's) is hidden_card(), and the RNG is reset (future shuffles are secret too). Ids carry no
// information: setup creates deck objects in shuffled order. Not playable: the engine needs real cards, so search
// plays on determinize(view(…)).
GameState view(const GameState& s, PlayerId p);

// One state consistent with a view: per owner, the hidden objects get the deck's cards that no visible object
// holds, at random (rune deck: its runes, everywhere else: the rest). The RNG is reseeded from `rng`. Throws
// std::logic_error if the view doesn't fit the decks (seat 0, seat 1 as in new_game).
GameState determinize(const GameState& view, const Deck& seat0, const Deck& seat1, Rng& rng);

} // namespace buddy::riftbound
