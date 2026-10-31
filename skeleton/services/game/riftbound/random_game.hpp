#pragma once
// Random-vs-random games over two decks, in vanilla mode or with the cards' rules: the engine's test bed (5.2), later
// 5.4's baseline. Both
// seats pick uniformly among legal_actions(); after every action the state must pass check_invariants() and the
// turn checks in play_random().

#include <cstdint>
#include <array>
#include <optional>
#include <string>

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
    std::string problem;  // first failed check: "turn 12, action 87 (play Blade Twirler): …"; empty = fine
};

RandomGame play_random(const Deck& seat0, const Deck& seat1, std::uint64_t seed, int max_actions = 100000,
                       bool vanilla = true);

} // namespace buddy::riftbound
