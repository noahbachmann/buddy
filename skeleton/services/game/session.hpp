#pragma once
// One Riftbound game between Buddy and Dev (5.6.2, #48): the referee's state, Buddy's table and AI, message-driven.
// No MQTT, no threads, no clock: the service (5.6.6) feeds it table events, Dev's commands and the AI's answers, and
// sends what take() returns. Design: docs/plans/05-riftbound.md → "5.6 design".
//
// Step protocol for one action a (Buddy's or Dev's):
//   1. next = s; apply(next, a) under MoveRecorder; TableMoves::step gives Buddy's table commands (deals split into
//      single cards, so a failed deal never leaves dealt cards without ids), and Dev's prompts join them in log order:
//      Dev's hidden cards that turn visible without Dev naming them (burned, channelled, discarded unasked).
//   2. The items run one at a time. Codes from deal/peek and Dev's answers are bound on s (bind(), knower = Buddy)
//      and next is re-applied; the items already done must be a prefix of the new list, else a desync (Buddy's
//      cards: pause for good) or a refusal (Dev's answer: nothing changes).
//   3. After the last item: s = next, the step's binds and the action go to the replay.
// The table always acts before the state commits; a step that fails before its first command never happened.
//
// Dev plays real cards (#49): the referee doesn't know Dev's hand or deck order, Dev's hidden objects hold
// placeholders. Dev chooses from dev_options(): the actions on known cards, and hidden hand cards by card (one
// option per card Dev could hold, checked on a copy with a hand object bound to it); choosing one binds it.

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "buddy/table_protocol.hpp"
#include "riftbound/card_db.hpp"
#include "riftbound/deck.hpp"
#include "riftbound/replay.hpp"
#include "riftbound/state.hpp"
#include "table_moves.hpp"

namespace buddy::game {

using riftbound::Action;

struct SessionConfig {
    std::array<riftbound::Deck, riftbound::player_count> decks;  // seat 0, seat 1 as in new_game
    std::array<std::string, riftbound::player_count> deck_texts;  // the decklists' text, for the replay
    PlayerId buddy = 0;
    std::uint64_t seed = 0;
};

enum class Status : std::uint8_t {
    Setup,          // the table lays out the game
    DevToAct,       // waiting for on_dev(): one of dev_options()
    DevToReveal,    // waiting for on_reveal(): Dev names the cards of prompt()
    BuddyThinking,  // an AiRequest is out
    Acting,         // table commands of a step (or a rescan, extras going back) are running
    Paused,         // pause(): resume() or concede()
    Over,
};
std::string_view to_string(Status s);

enum class Pause : std::uint8_t {
    None,
    Unreadable,  // a deal or peek stayed unreadable after one retry
    Hardware,    // the table failed: scans never agree, the spot doesn't empty, …
    Missing,     // a card of Buddy's the table can't find
    Invalid,     // the table refused a command (a bug)
    Desync,      // the engine and the table disagree; can't resume
};
std::string_view to_string(Pause p);

// Dev's cards that turn visible without Dev choosing them: Dev names each one (#49).
struct DevPrompt {
    enum class Kind : std::uint8_t {
        Battlefield,  // the one Dev chose, at setup before the mulligans
        Hand,         // left Dev's hand unasked (a discard with no choice)
        Deck,         // off Dev's main deck (burned)
        Runes,        // channelled: any order
    };
    Kind kind = Kind::Deck;
    std::vector<ObjId> objs;  // answer with one card each, in this order
    bool operator==(const DevPrompt&) const = default;
};
std::string_view to_string(DevPrompt::Kind k);

// One of Dev's choices. A hidden hand card of Dev's is offered by card: `obj` (the session's pick of Dev's hidden
// hand objects, the one `action` names) becomes `card` when chosen. Unnamed: an action that needs some card in Dev's
// hand that the placeholders don't hold (Mel's "discard a spell"): `obj` gets `card` as a placeholder, nothing more.
struct DevOption {
    Action action;
    ObjId obj = riftbound::no_obj;
    const riftbound::Card* card = nullptr;
    bool named = true;
    bool operator==(const DevOption&) const = default;
};
std::string describe(const riftbound::GameState& s, const DevOption& o);

// Dev's input refused (not an option, an impossible card): the reason, nothing changed.
class Refused : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

// The AI's question: answer with on_ai(id, one of actions). `view` = view(s, buddy), never the referee's state.
struct AiRequest {
    std::uint64_t id = 0;
    riftbound::GameState view;
    std::vector<Action> actions;
};

// What the inputs since the last take() produced.
struct Outputs {
    std::vector<table::Command> table;  // in order; each waits for the previous one's end, so at most one
    std::optional<AiRequest> ai;
    std::vector<std::string> voice;
    std::vector<std::string> replay;    // lines to append to the replay file (header first)
};

struct SessionStats {
    long steps = 0, commands = 0, binds = 0, reapplies = 0, re_decides = 0, retries = 0, corrections = 0,
         misreads = 0, reorders = 0, extras = 0, pauses = 0, resumes = 0;
    long dev_decisions = 0, dev_forced = 0, dev_named = 0, prompts = 0, refusals = 0;
};

class Session {
public:
    Session(const riftbound::CardDb& db, SessionConfig cfg);

    // Inputs. Each one may produce outputs; take() them after every call.
    void start();
    void on_table(const table::Event& e);  // events of other seqs are ignored
    // Dev's choice, one of dev_options(). Throws std::logic_error if it isn't Dev's decision, Refused if it isn't an
    // option (nothing changes either way).
    void on_dev(const DevOption& o);
    // The cards of prompt(), in its order. Throws std::logic_error without a prompt, Refused if the cards can't be
    // (no copy of it left that Buddy hasn't seen, the wrong kind, or the step couldn't have gone this way).
    void on_reveal(const std::vector<const riftbound::Card*>& cards);
    void on_ai(std::uint64_t id, const Action& a);  // a stale id is ignored; an action not offered throws
    // Paused (not Desync): rescan the hand, then run the command that failed again. Throws std::logic_error otherwise.
    void resume();
    void concede(PlayerId p);

    Outputs take() { return std::exchange(out_, {}); }

    Status status() const { return status_; }
    // DevToAct: what Dev may do. Applied unasked when there's one option only (no card in Dev's hand could change it).
    const std::vector<DevOption>& dev_options() const { return dev_options_; }
    const DevPrompt* prompt() const;  // DevToReveal: what Dev names
    Pause pause() const { return pause_; }
    const std::string& pause_message() const { return pause_message_; }
    // The committed referee state (between steps: the state before the running step).
    const riftbound::GameState& state() const { return s_; }
    PlayerId buddy() const { return cfg_.buddy; }
    int table_id(ObjId obj) const;  // 0: the table has no id for it
    ObjId obj_of(int id) const;     // no_obj: not one of the engine's
    // Buddy's hand as the table believes it, bottom → top (table ids).
    const std::vector<int>& table_hand() const { return table_hand_; }
    const riftbound::Replay& replay() const { return replay_; }
    std::string replay_text() const;  // every replay line so far
    const SessionStats& stats() const { return stats_; }

    // Tests only: sees every computed command list of a step (forced divergence).
    std::function<void(std::vector<TableMove>&)> on_moves;

private:
    // One item of a step: Buddy's table command, or Dev's prompt.
    struct Item {
        TableMove move;
        std::optional<DevPrompt> prompt;
    };
    struct Step {
        std::optional<Action> action;  // nullopt: the opening deals after setup
        bool chosen = false;           // Buddy's AI chose it: not_in_hand on its play → decide again
        riftbound::GameState next;
        TableMoves moves{0};
        std::vector<Item> list;
        std::size_t done = 0;
        bool stale = false;  // a bind changed s since the list was computed: re-apply before the next command
    };
    enum class Kind : std::uint8_t { Setup, Step, Fix, Rescan };
    struct Inflight {
        table::Command cmd;
        Kind kind = Kind::Step;
        int attempt = 0;
    };

    void pump();
    bool advance();  // true: a forced step began
    void begin_step(const Action& a, bool chosen);
    bool recompute();  // false: desync (paused)
    // The step's action applied on `base`: next, moves and items into `out`; the first `through` items must stay as
    // they were. Returns why not, empty = fine.
    std::string compute(const riftbound::GameState& base, std::size_t through, Step& out) const;
    std::vector<Item> items(const riftbound::GameState& base, const riftbound::GameState& next,
                            const std::vector<riftbound::LoggedMove>& log, std::vector<TableMove> moves) const;
    std::vector<DevOption> options() const;
    bool dev_hidden(ObjId obj) const;  // a hand card of Dev's Buddy doesn't know and Dev hasn't named
    void take_dev(const DevOption& o);
    void bind_line(ObjId obj, const riftbound::Card* card, bool kept);
    void send(table::Command c, Kind kind, int attempt = 0);
    void send_step_command();
    void done(const Inflight& f, const table::Event& e);
    void failed(const Inflight& f, const table::Event& e);
    void correction(const table::Event& e);
    void reveal(ObjId obj, const std::string& code);
    void commit();
    void reconcile_hand();
    void do_pause(Pause p, const std::string& message);
    void map_id(ObjId obj, int id);
    void forget(ObjId obj);
    void emit_replay(const std::string& line);

    const riftbound::CardDb& db_;
    SessionConfig cfg_;
    riftbound::GameState s_;
    TableMoves moves_;
    std::optional<Step> step_, opening_;
    TableSetup setup_;
    std::optional<Inflight> inflight_;
    table::Command setup_cmd_;
    bool set_up_ = false;  // the table took the setup
    int attempt_ = 0;      // of the current step command: an unreadable one runs once more
    std::int64_t seq_ = 0;
    std::uint64_t ai_id_ = 0;
    std::vector<Action> ai_actions_;
    Status status_ = Status::Setup;
    Pause pause_ = Pause::None;
    std::string pause_message_;
    bool pause_after_ = false;      // a correction reported missing cards: pause once the command ends
    int re_decides_ = 0;            // for the current decision
    std::map<ObjId, int> id_;       // engine object → table id
    std::map<int, ObjId> obj_;      // table id → engine object
    std::map<ObjId, std::string> missing_;  // objects the table lost, by code: found again as an extra → re-mapped
    std::vector<int> table_hand_;
    std::deque<int> fixes_;         // extra cards (table ids) going back on the deck top
    ObjId peeking_ = riftbound::no_obj;
    riftbound::BindMemory bound_;   // bind()'s keep, tracked as run_replay does
    std::set<ObjId> named_;         // Dev's hidden cards Dev named (a discard picked by card): no prompt for them
    std::vector<DevOption> dev_options_;
    riftbound::Replay replay_;
    std::vector<std::string> lines_;
    Outputs out_;
    SessionStats stats_;
};

} // namespace buddy::game
