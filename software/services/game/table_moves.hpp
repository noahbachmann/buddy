#pragma once
// Engine → table (2.5, #42): what Buddy's physical cards do in one referee step, as table commands. The engine reports
// its moves through riftbound::move_log (state.hpp); a state diff can't order a Burn Out between two draws or say which
// recycled card went down first. Design: docs/02-riftbound/design.md → "2.5 design". 2.6's game service uses this as is.

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "buddy/table_protocol.hpp"
#include "riftbound/deck.hpp"
#include "riftbound/state.hpp"

namespace buddy::game {

using riftbound::GameState;
using riftbound::LoggedMove;
using riftbound::ObjId;
using riftbound::PlayerId;

// Installs a move log for this thread while it lives.
class MoveRecorder {
public:
    MoveRecorder() { riftbound::move_log = &log_; }
    ~MoveRecorder() { riftbound::move_log = nullptr; }
    MoveRecorder(const MoveRecorder&) = delete;
    MoveRecorder& operator=(const MoveRecorder&) = delete;
    // The moves since the last take().
    std::vector<LoggedMove> take() { return std::exchange(log_, {}); }

private:
    std::vector<LoggedMove> log_;
};

// A table command with engine objects where the table has ids: Play/Move/Remove name `objs[0]` (cmd.id unset),
// Exhaust `objs` (cmd.ids unset); Deal, Peek and Token list the objects they bring, in order.
struct TableMove {
    table::Command cmd;
    std::vector<ObjId> objs;
    // Index into the step's log of the move it comes from (#49: Dev's prompts go between the commands in log order):
    // a collapsed hop its last one, a merged deal its first card's; peek_done of the last step 0, exhaust/ready the
    // log's size.
    std::size_t at = 0;
};

// Of same-card copies in Buddy's hand, play the one nearest the bottom of the pile: fewer feeds, and the engine
// removes the copy the table takes (the table plays exactly the id it's asked for). Other actions unchanged. Call it
// on the seat's chosen action before apply; the AI keys hand cards by card, so it's the same move.
riftbound::Action nearest_copy(const GameState& s, riftbound::Action a);

// The printed code the table reads for a card: its home printing; tokens "token:<name>".
std::string code_of(const GameState& s, ObjId id);

// What the table starts with for `seat`, from new_game's log: legend, champion, decks bottom → top.
struct TableSetup {
    ObjId legend = riftbound::no_obj, champion = riftbound::no_obj;
    std::vector<ObjId> deck, runes;
};
// Takes the setup entries off the front of `log` (created objects); the rest are new_game's draws.
TableSetup table_setup(const GameState& s, std::vector<LoggedMove>& log, PlayerId seat);

// One seat's physical moves, step by step. Remembers what the table shows exhausted.
class TableMoves {
public:
    explicit TableMoves(PlayerId seat) : seat_(seat) {}

    // The commands for one step: `after` = the state after it, `log` = what moved during it. In physical order;
    // hops through the chain or "set aside" in one step collapse (hand → chain → base = one play), consecutive deck
    // → same place = one deal, then exhaust/ready to match `after`. Vision: `peek` as the engine shows the seat its top
    // card (the step ends with the keep-or-recycle decision open; the session binds the scanned card before the AI
    // answers), then the answer's step starts with `peek_done` to the deck top or bottom.
    std::vector<TableMove> step(const GameState& after, const std::vector<LoggedMove>& log);

private:
    PlayerId seat_;
    ObjId peeking_ = riftbound::no_obj;  // on the scan spot until the next step
    std::vector<std::uint8_t> exhausted_;  // by ObjId: what the table shows
};

// The workload for buddy-table --soak (#43): one game seen from `seat` with greedy or random players, as JSON lines:
// first {"setup": {seat, legend, champion, deck, runes (codes bottom → top), objs: {…}}}, then per command
// {"cmd": <table command>, "objs": [...]}, last {"end": {winner, turns}}. problem non-empty: the game failed.
struct WorkloadGame {
    std::vector<nlohmann::json> lines;
    std::string problem;
};
WorkloadGame workload_game(const riftbound::Deck& seat0, const riftbound::Deck& seat1, PlayerId seat,
                           std::uint64_t seed, bool greedy);

} // namespace buddy::game
