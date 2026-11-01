#include "arena.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <thread>

#include "riftbound/engine.hpp"
#include "riftbound/hidden.hpp"
#include "riftbound/random_game.hpp"

namespace buddy::riftbound::ai {

ArenaGame play_game(const Deck& seat0, const Deck& seat1, const std::array<Player*, player_count>& players,
                    std::uint64_t seed, int max_actions) {
    ArenaGame result;
    std::string last = "setup";
    try {
        GameState s = new_game(seat0, seat1, {.seed = seed});
        for (PlayerId p = 0; p < player_count; ++p) players[p]->begin(p, seat0, seat1);
        GameChecks checks(s);
        if (auto p = checks.check(s); !p.empty()) throw std::logic_error(p);
        std::array<int, player_count> run{};  // sub-decisions since the seat's last Priority action
        while (!s.winner) {
            if (result.actions == max_actions) throw std::logic_error("no winner yet");
            const PlayerId p = s.decision.player;
            SeatStats& stats = result.seats[p];
            const auto actions = legal_actions(s);
            stats.legal.push_back(static_cast<int>(actions.size()));
            const bool sub = s.decision.kind == DecisionKind::Choose || s.decision.kind == DecisionKind::Recycle;
            if (sub) {
                ++run[p];
            } else if (run[p] > 0) {
                stats.choose_runs.push_back(run[p]);
                run[p] = 0;
            }
            Action a = actions.at(0);
            if (actions.size() == 1) {
                ++stats.forced;
            } else {
                const GameState seen = view(s, p);
                const auto start = std::chrono::steady_clock::now();
                a = players[p]->choose(seen, actions);
                stats.ms.push_back(
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            }
            last = describe(s, a);
            result.log.push_back(a);
            apply(s, a);
            ++result.actions;
            result.turns = s.turn;
            if (auto problem = checks.check(s); !problem.empty()) throw std::logic_error(problem);
        }
        result.winner = s.winner;
        for (PlayerId p = 0; p < player_count; ++p) result.points[p] = s.player(p).points;
    } catch (const std::exception& e) {
        result.problem = "turn " + std::to_string(result.turns) + ", action " + std::to_string(result.actions) +
                         " (" + last + "): " + e.what();
    }
    return result;
}

Pairing pairing(std::uint64_t first_seed, int game) {
    const auto pair = static_cast<std::uint64_t>(game / 2);
    return {first_seed + pair, game % 2 == 1, pair % 2 == 1};
}

std::optional<int> MatchGame::winning_player() const {
    if (!game.winner) return std::nullopt;
    return (*game.winner == 0) == pairing.b_in_seat0 ? 1 : 0;
}

std::optional<int> MatchGame::winning_deck() const {
    if (!game.winner) return std::nullopt;
    return (*game.winner == 0) == pairing.decks_swapped ? 1 : 0;
}

std::vector<MatchGame> play_match(const PlayerFactory& a, const PlayerFactory& b, const Deck& deck_a,
                                  const Deck& deck_b, int games, std::uint64_t first_seed, int jobs) {
    std::vector<MatchGame> results(static_cast<std::size_t>(games));
    std::atomic<int> next{0};
    auto work = [&] {
        for (int i = next++; i < games; i = next++) {
            const Pairing pr = pairing(first_seed, i);
            // per seed and player, not per game: a swapped rematch gets the same player seeds
            auto pa = a(pr.seed * 2), pb = b(pr.seed * 2 + 1);
            std::array<Player*, player_count> seats{pa.get(), pb.get()};
            if (pr.b_in_seat0) std::swap(seats[0], seats[1]);
            const Deck& d0 = pr.decks_swapped ? deck_b : deck_a;
            const Deck& d1 = pr.decks_swapped ? deck_a : deck_b;
            results[static_cast<std::size_t>(i)] = {pr, play_game(d0, d1, seats, pr.seed)};
        }
    };
    std::vector<std::jthread> threads;
    for (int j = 1; j < std::max(1, jobs); ++j) threads.emplace_back(work);
    work();
    return results;
}

std::array<double, 2> wilson(int wins, int n) {
    if (n == 0) return {0, 1};
    constexpr double z = 1.959964;
    const double p = static_cast<double>(wins) / n, z2n = z * z / n;
    const double centre = (p + z2n / 2) / (1 + z2n);
    const double half = z * std::sqrt(p * (1 - p) / n + z2n / (4 * n)) / (1 + z2n);
    return {centre - half, centre + half};
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(p / 100 * static_cast<double>(values.size())));
    return values[std::clamp<std::size_t>(rank, 1, values.size()) - 1];
}

} // namespace buddy::riftbound::ai
