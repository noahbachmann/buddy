#pragma once
// Random-vs-random games over two decks, in vanilla mode or with the cards' rules: the engine's test bed (5.2), later
// 5.4's baseline. Both
// seats pick uniformly among legal_actions(); after every action the state must pass check_invariants() and the
// turn checks in play_random().

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "deck.hpp"
#include "state.hpp"

namespace buddy::riftbound {

struct RandomGame {
    std::optional<PlayerId> winner;
    int turns = 0;
    int actions = 0;
    std::array<int, player_count> points{};
    int spells = 0;     // spells played
    int reactions = 0;  // cards played while a chain existed (closed state)
    int activations = 0;  // activated abilities used
    int triggers = 0;     // triggered abilities that went on the chain
    int choices = 0;      // Choose decisions answered (targets, may, costs, order, effects)
    int moves = 0;        // Standard Moves
    int showdowns = 0;    // showdowns begun, combats included (lower bound, see play_random)
    int combats = 0;
    int scorings = 0;     // battlefields conquered or held (469; lower bound)
    std::string problem;  // first failed check: "turn 12, action 87 (play Blade Twirler): …"; empty = fine
    std::vector<Action> log;  // every action tried, the failing one included: a Replay's actions
};

// What play_random() checks after every action, also run by run_replay(): check_invariants(), a constant card count
// (tokens come and go, 186.1), and at each turn's first open Main-phase priority a readied board and empty pools.
class GameChecks {
public:
    explicit GameChecks(const GameState& start);
    std::string check(const GameState& s);  // the first problem; empty = fine

private:
    long cards_;
    int checked_turn_ = 0;
};

RandomGame play_random(const Deck& seat0, const Deck& seat1, std::uint64_t seed, int max_actions = 100000,
                       bool vanilla = true);

} // namespace buddy::riftbound
