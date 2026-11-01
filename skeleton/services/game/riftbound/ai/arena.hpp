#pragma once
// The arena (5.4): AI players against each other on the referee's state. Each seat gets only its view; a decision
// with one legal action is applied without asking (forced). GameChecks after every action, the action log for a
// replay on failure. `buddy-game --match` runs matches; the tests use play_game directly.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "player.hpp"

namespace buddy::riftbound::ai {

// One seat's decisions in a game.
struct SeatStats {
    int forced = 0;
    std::vector<int> legal;       // legal actions per decision, forced ones included
    std::vector<double> ms;       // time per asked decision
    std::vector<double> iterations;  // per asked decision of a searching player
    std::vector<int> choose_runs;  // sub-decisions (Choose, Recycle) in a row after a Priority action, if any
};

struct ArenaGame {
    std::optional<PlayerId> winner;
    int turns = 0;
    int actions = 0;
    std::array<int, player_count> points{};
    std::array<SeatStats, player_count> seats;
    std::string problem;      // as RandomGame::problem; empty = fine
    std::vector<Action> log;  // every action tried, the failing one included
};

// One game with rules (no vanilla): players[p] sits in seat p. Calls begin() on both.
ArenaGame play_game(const Deck& seat0, const Deck& seat1, const std::array<Player*, player_count>& players,
                    std::uint64_t seed, int max_actions = 100000);

// A fresh player per game and seat, seeded so a match gives the same games however many jobs run it.
using PlayerFactory = std::function<std::unique_ptr<Player>(std::uint64_t seed)>;

// Game i of a match: seed S + i / 2, so every seed is played twice with the players swapped (same shuffles, same
// decks per seat); decks swap seats every other seed. So both players play both decks from both seats.
struct Pairing {
    std::uint64_t seed = 0;
    bool b_in_seat0 = false;  // player B sits in seat 0
    bool decks_swapped = false;  // seat 0 plays deck B
};
Pairing pairing(std::uint64_t first_seed, int game);

struct MatchGame {
    Pairing pairing;
    ArenaGame game;
    // Winner as a player (0 = A, 1 = B) and which deck (0 = deck A, 1 = deck B) won; empty = failed.
    std::optional<int> winning_player() const;
    std::optional<int> winning_deck() const;
};

// `games` games of A against B with decks `deck_a`, `deck_b`, `jobs` games in parallel; results in game order.
std::vector<MatchGame> play_match(const PlayerFactory& a, const PlayerFactory& b, const Deck& deck_a,
                                  const Deck& deck_b, int games, std::uint64_t first_seed, int jobs);

// 95 % Wilson score interval of `wins` out of `n`: {low, high}.
std::array<double, 2> wilson(int wins, int n);

// p-th percentile (0..100) by nearest rank; 0 for an empty list.
double percentile(std::vector<double> values, double p);

} // namespace buddy::riftbound::ai
