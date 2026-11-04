#pragma once
// Engine → table (5.5, #42): what Buddy's physical cards do in one referee step, as table commands. The engine reports
// its moves through riftbound::move_log (state.hpp); a state diff can't order a Burn Out between two draws or say which
// recycled card went down first. Design: docs/plans/05-riftbound.md → "5.5 design". 5.6's game service uses this as is.

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
    // → same place = one deal, deck → deck bottom = peek + peek_done, then exhaust/ready to match `after`.
    std::vector<TableMove> step(const GameState& after, const std::vector<LoggedMove>& log);

private:
    PlayerId seat_;
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
