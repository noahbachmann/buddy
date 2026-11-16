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
//
// A service game (5.6) also has the identities the outside world revealed, applied before the next action line:
//
//   buddy 1                    (header: the seat whose knowledge `bind` uses, bind() in state.hpp)
//   bind 41 OGN-007a           # Vi's object 41 is that card: a scan, or Dev's entry
//   order 1 12 40 33 7         # player 1's hand order bottom → top (set_hand_order)

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "card_db.hpp"
#include "state.hpp"

namespace buddy::riftbound {

// An identity revealed between two actions (5.6): a bind (obj is the card printed `code`) or a hand order.
struct Reveal {
    enum class Kind : std::uint8_t { Bind, Order };
    std::size_t before = 0;  // applied before actions[before]; actions.size() = after the last
    Kind kind = Kind::Bind;
    ObjId obj = no_obj;      // Bind
    std::string code;        // Bind: Code::str()
    PlayerId player = 0;     // Order
    std::vector<ObjId> ids;  // Order: the hand bottom → top
    bool operator==(const Reveal&) const = default;
};

struct Replay {
    std::uint64_t seed = 0;
    bool vanilla = false;
    std::array<std::string, player_count> decks;  // decklist text per seat
    std::vector<Action> actions;
    std::optional<PlayerId> buddy;  // whose knowledge binds use; needed once there are binds
    std::vector<Reveal> reveals;    // ascending `before`
};

// A malformed replay: "line 12: …".
class ReplayError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Applies one reveal; throws std::logic_error if the code is unknown, the bind or the order impossible. `group`: the
// objects bound by earlier reveals before the same action (bind()'s `keep`); a bind adds its object.
void apply_reveal(GameState& s, const Reveal& r, const CardDb& db, PlayerId buddy, std::vector<ObjId>* group = nullptr);

// One line of the actions section: "play 41", "bind 41 OGN-007a", "order 1 12 40". A service appends them as it goes.
std::string format_action(const Action& a);
std::string format_reveal(const Reveal& r);
// The header up to and including "actions": seed, vanilla, buddy, both decklists.
std::string format_header(const Replay& r);

Replay parse_replay(std::string_view text);  // throws ReplayError
// Every action annotated with turn, player and describe(), as far as the game replays.
std::string to_text(const Replay& replay, const CardDb& db);

struct ReplayResult {
    GameState state;    // after the last action applied
    int applied = 0;
    std::string problem;  // as RandomGame::problem; empty = every action applied and every check passed
};
// Plays a replay with play_random's checks (GameChecks) after every action. `each` sees the state before each
// action (after that action's reveals). Throws DeckError for a deck that doesn't parse; everything later is a `problem`.
ReplayResult run_replay(const Replay& replay, const CardDb& db,
                        const std::function<void(const GameState&, const Action&)>& each = {});

} // namespace buddy::riftbound
