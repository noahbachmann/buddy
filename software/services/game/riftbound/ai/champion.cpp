#include "champion.hpp"

#include <atomic>
#include <thread>

#include "../rng.hpp"
#include "arena.hpp"
#include "greedy.hpp"

namespace buddy::riftbound::ai {

ChampionPick choose_champion(const Deck& mine, const Deck& theirs, std::uint64_t seed, ChampionOptions o) {
    ChampionPick pick;
    pick.options = champion_options(mine);
    pick.card = pick.options.front();
    pick.wins.assign(pick.options.size(), 0);
    if (pick.options.size() < 2 || o.games <= 0) return pick;
    pick.games = o.games;

    std::vector<Deck> decks;
    for (const Card* c : pick.options) decks.push_back(with_champion(mine, c));
    const auto their_options = champion_options(theirs);
    std::vector<Deck> opponents;  // per game, the same for every option
    Rng rng(seed);
    for (int g = 0; g < o.games; ++g) opponents.push_back(with_champion(theirs, their_options[rng.below(their_options.size())]));

    const int total = static_cast<int>(decks.size()) * o.games;
    std::vector<char> won(static_cast<std::size_t>(total), 0);
    std::atomic<int> next{0};
    auto work = [&] {
        for (int i = next++; i < total; i = next++) {
            const int option = i / o.games, g = i % o.games;
            const PlayerId me = static_cast<PlayerId>(g % 2);
            const std::uint64_t game_seed = seed + 1 + static_cast<std::uint64_t>(g);
            GreedyPlayer a(game_seed * 2), b(game_seed * 2 + 1);
            std::array<Player*, player_count> seats{&a, &b};
            const Deck& d0 = me == 0 ? decks[option] : opponents[g];
            const Deck& d1 = me == 0 ? opponents[g] : decks[option];
            const ArenaGame game = play_game(d0, d1, seats, game_seed);
            won[static_cast<std::size_t>(i)] = game.winner == me;
        }
    };
    {
        std::vector<std::jthread> threads;
        for (int j = 1; j < std::max(1, o.jobs); ++j) threads.emplace_back(work);
        work();
    }
    for (int i = 0; i < total; ++i) pick.wins[static_cast<std::size_t>(i / o.games)] += won[static_cast<std::size_t>(i)];
    std::size_t best = 0;
    for (std::size_t k = 1; k < pick.wins.size(); ++k) {
        if (pick.wins[k] > pick.wins[best]) best = k;
    }
    pick.card = pick.options[best];
    return pick;
}

} // namespace buddy::riftbound::ai
