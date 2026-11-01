#pragma once
// How good a state is for one player (5.4): v1 greedy compares the states its actions lead to, v2 ISMCTS scores its
// truncated playouts with it. Reads only what that player's view shows too (hand sizes, not hand cards).

#include "riftbound/state.hpp"

namespace buddy::riftbound::ai {

// Hand-set; tuned by self-play in #38. Every term is the player's minus the opponent's.
struct Weights {
    double point = 10;        // per point
    double near_win = 3;      // × (points past victory_score − 3)²: the last points weigh more
    double control = 4;       // per battlefield controlled: it holds for a point next turn
    double unit = 2;          // per unit on the board
    double might = 1;         // per Might of those units
    double might_field = 0.5;  // per Might at a battlefield, on top: it defends or scores there
    double gear = 1.5;        // per gear on the board
    double hand = 1.5;        // per card in hand
    double rune = 1.5;        // per rune in the base: all of them are ready next turn
    double legend_ready = 0.5;  // legend not exhausted: its ability is there for the opponent's turn
};

// p's advantage; +∞ if p won, −∞ if p lost.
double eval(const GameState& s, PlayerId p, const Weights& w = {});

} // namespace buddy::riftbound::ai
