#pragma once
// v1 greedy (5.4): per legal action, apply it on one determinization, play on to quiescence and eval(). The best
// wins, ties by action order. Quiescence = the game is over or someone has Neutral Open priority (the main phase
// with an empty chain and no showdown). On the way the deciding player's own sub-decisions (targets, costs, picks)
// are chosen greedily too (nested up to GreedyOptions::depth), the opponent passes and declines every choice.

#include <array>
#include <vector>

#include "eval.hpp"
#include "player.hpp"

namespace buddy::riftbound::ai {

struct GreedyOptions {
    Weights weights;
    int depth = 2;            // nested greedy sub-decisions in a playout; deeper ones: the default answer
    int max_playout = 400;    // actions per playout before it's scored as it is
    int mulligan_energy = 5;  // set aside opening-hand cards that cost at least this much Energy (117)
};

// The greedy choice at s's decision, on the full state (a determinization, or a test's state): one of
// legal_actions(s). EndTurn is scored as the state now: ending the turn changes nothing of one's own, and playing
// on into the opponent's turn would charge it alone with what they do there.
Action greedy_choice(const GameState& s, const GreedyOptions& options = {});

// Mulligan by rule, not search: the first legal pick costing ≥ mulligan_energy, else Done.
Action mulligan_choice(const GameState& s, const std::vector<Action>& actions, int mulligan_energy);

// greedy_choice on one determinization of the view per decision.
class GreedyPlayer : public Player {
public:
    explicit GreedyPlayer(std::uint64_t seed, GreedyOptions options = {}) : rng_(seed), options_(options) {}
    std::string name() const override { return "greedy"; }
    void begin(PlayerId seat, const Deck& seat0, const Deck& seat1) override;
    Action choose(const GameState& view, const std::vector<Action>& actions) override;

private:
    Rng rng_;
    GreedyOptions options_;
    std::array<Deck, player_count> decks_;
};

} // namespace buddy::riftbound::ai
