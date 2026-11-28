#pragma once
// Buddy's Chosen Champion (#98): picked at game start out of champion_options(deck), by greedy self-play.
#include <cstdint>
#include <vector>

#include "../deck.hpp"

namespace buddy::riftbound::ai {

struct ChampionOptions {
    int games = 24;  // per option, both seats
    int jobs = 1;    // threads; the result doesn't depend on them
};

struct ChampionPick {
    const Card* card = nullptr;
    std::vector<const Card*> options;  // champion_options(mine)
    std::vector<int> wins;             // per option, out of `games`
    int games = 0;
};

// Per option of `mine`, `games` greedy-vs-greedy games against `theirs` (seats alternate), the opponent's champion
// drawn at random among its options: Buddy picks first, without knowing it. Every option plays the same seeds, the
// same seats and the same opponent champions (common random numbers: the difference is the champion, less the
// noise). Most wins; ties → the earlier option (the decklist's champion first). One option: no games.
ChampionPick choose_champion(const Deck& mine, const Deck& theirs, std::uint64_t seed, ChampionOptions o = {});

} // namespace buddy::riftbound::ai
