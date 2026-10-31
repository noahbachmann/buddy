#pragma once
// The rules engine's one loop, the same for the AI, the panel and the tests: the engine runs until a player has to
// choose, stops at GameState::decision, and legal_actions() / apply() answer it. Design: docs/plans/05-riftbound.md
// → "5.2 design".

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "deck.hpp"
#include "state.hpp"

namespace buddy::riftbound {

struct GameOptions {
    std::uint64_t seed = 0;
    // Stats only, card text ignored: random games over the real decks before their cards exist (5.2). Tests only;
    // without it every card needs its implementation (D21).
    bool vanilla = false;
};

// Card has rules the engine can run: until 5.3 only the basic runes (164).
bool is_implemented(const Card& card);

// A move that legal_actions() doesn't offer right now.
class IllegalAction : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

// Setup 110–117 with Duel's 487.5 for seats 0 and 1: legends and chosen champions in their zones, one random
// battlefield each (the other two removed), decks shuffled, random first player, everyone draws 4, then stops at the
// first player's mulligan. Throws std::runtime_error naming every unimplemented card (not with options.vanilla).
GameState new_game(const Deck& seat0, const Deck& seat1, const GameOptions& options);

// Everything decision.player may do now, in a fixed order; empty when nothing is to be decided.
std::vector<Action> legal_actions(const GameState& s);
// Answers the decision and runs the engine to the next one. Throws IllegalAction (state unchanged).
void apply(GameState& s, const Action& action);

// "set aside Blade Twirler", "done": for logs, tests and the panel.
std::string describe(const GameState& s, const Action& action);

} // namespace buddy::riftbound
