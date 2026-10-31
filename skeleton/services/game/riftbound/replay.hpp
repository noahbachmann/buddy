#pragma once
// Replays: seed + decklists + action list reproduce a game exactly (the RNG lives in the state, rng.hpp), so a replay
// is the bug-report format. Text, one action per line, comments after "#":
//
//   # buddy-game --replay <file>
//   seed 17
//   vanilla
//   deck 0
//   Legend:
//   1 Vi, Piltover Enforcer
//   …                          (decks/*.txt format, deck.hpp)
//   end
//   deck 1
//   …
//   end
//   actions
//   done                       # turn 0, player 1: keep
//   play 41                    # turn 1, player 1: play Blade Twirler
//   activate 3 1               # <kind> [<obj>|-] [<n>]
//   move - 255                 # obj "-" = no_obj (here: to base)

#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "card_db.hpp"
#include "state.hpp"

namespace buddy::riftbound {

struct Replay {
    std::uint64_t seed = 0;
    bool vanilla = false;
    std::array<std::string, player_count> decks;  // decklist text per seat
    std::vector<Action> actions;
};

// A malformed replay: "line 12: …".
class ReplayError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Replay parse_replay(std::string_view text);  // throws ReplayError
// Every action annotated with turn, player and describe(), as far as the game replays.
std::string to_text(const Replay& replay, const CardDb& db);

struct ReplayResult {
    GameState state;    // after the last action applied
    int applied = 0;
    std::string problem;  // as RandomGame::problem; empty = every action applied and every check passed
};
// Plays a replay with play_random's checks (GameChecks) after every action. `each` sees the state before each
// action. Throws DeckError for a deck that doesn't parse; everything later is a `problem`.
ReplayResult run_replay(const Replay& replay, const CardDb& db,
                        const std::function<void(const GameState&, const Action&)>& each = {});

} // namespace buddy::riftbound
