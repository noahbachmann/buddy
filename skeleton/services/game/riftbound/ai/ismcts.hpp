#pragma once
// v2 ISMCTS (5.4): single-observer information-set MCTS. One tree over action keys for the deciding player's
// information set; every iteration plays on a fresh determinization of the view, so the opponent's hidden cards
// differ per iteration. Selection: UCB1 over the actions available in this determinization (availability counts),
// one expansion, a truncated playout scored by eval() squashed to [0, 1]. Root parallelization: a tree and Rng per
// thread, root visits summed, no locks. Accepted weakness: in the tree the opponent plays as if it saw our hand
// (strategy fusion). Design: docs/plans/05-riftbound.md → "5.4 design".

#include <array>
#include <cstdint>
#include <vector>

#include "eval.hpp"
#include "player.hpp"

namespace buddy::riftbound::ai {

struct IsmctsOptions {
    int iterations = 1000;    // per decision, all threads together; ignored if budget_ms > 0
    double budget_ms = 0;     // time per decision instead (Pi): not reproducible
    int threads = 1;
    double exploration = 0.4;  // UCB1 c, rewards in [0, 1]; tuned in #38
    int playout = 10;         // random actions after the expansion, within the turn; then declined to quiescence
    double scale = 20;        // reward = 1 / (1 + exp(−eval / scale)); a point is worth 10
    int mulligan_energy = 5;  // mulligan by rule, as greedy
    Weights weights{};
};

struct IsmctsResult {
    Action action;
    int iterations = 0;
    std::vector<int> visits;  // per action of the given list (merged keys: the first of them counts)
};

// The choice at `view`'s decision (view(s, p), p = the deciding player) among `actions` = legal_actions(s). Same
// seed, options and thread count → same result under an iteration budget.
IsmctsResult ismcts_search(const GameState& view, const std::vector<Action>& actions, const Deck& seat0,
                           const Deck& seat1, std::uint64_t seed, const IsmctsOptions& options = {});

class IsmctsPlayer : public Player {
public:
    explicit IsmctsPlayer(std::uint64_t seed, IsmctsOptions options = {}) : rng_(seed), options_(options) {}
    std::string name() const override { return "ismcts"; }
    void begin(PlayerId seat, const Deck& seat0, const Deck& seat1) override;
    Action choose(const GameState& view, const std::vector<Action>& actions) override;
    int iterations() const override { return iterations_; }

private:
    Rng rng_;
    IsmctsOptions options_;
    std::array<Deck, player_count> decks_;
    int iterations_ = 0;
};

} // namespace buddy::riftbound::ai
