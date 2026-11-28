#pragma once
// One Riftbound game between Buddy and Dev (2.6.2, #48): the referee's state, Buddy's table and AI, message-driven.
// No MQTT, no threads, no clock: the service (2.6.6) feeds it table events, Dev's commands and the AI's answers, and
// sends what take() returns. Design: docs/02-riftbound/design.md → "2.6 design".
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
// option per card Dev could hold, checked on a copy with a hand object bound to it); choosing one binds it. The
// referee never picks from Dev's placeholders (#73): Dev's discard costs are always asked (GameOptions::ask_discard),
// and a cost that needs a spell is declared with the option ("discard X").

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
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
    Timeout,     // the table didn't answer in time (#52): the command stays in flight, a late answer goes on
    TableRestart,  // the table restarted mid-game and lost its piles (#52); can't resume
};
std::string_view to_string(Pause p);
// What Buddy says on a pause, and what the panel shows (the message stays in the journal: it may name a card).
std::string pause_text(Pause p);

// Dev's cards that turn visible without Dev choosing them: Dev names each one (#49).
struct DevPrompt {
    enum class Kind : std::uint8_t {
        Champion,     // the Chosen Champion Dev put down (#98), at setup first; only with more than one option
        Battlefield,  // the one Dev chose, at setup before the mulligans
        Hand,         // left Dev's hand unasked (a discard with no choice)
        Deck,         // off Dev's main deck (burned)
        Runes,        // channelled: any order
    };
    Kind kind = Kind::Deck;
    std::vector<ObjId> objs;  // answer with one card each, in this order
    std::vector<ObjId> known;  // Runes: channelled with them, already known to Buddy (recycled in view), not asked
    bool operator==(const DevPrompt&) const = default;
};
std::string_view to_string(DevPrompt::Kind k);

// One of Dev's choices. `card` is what Dev declares (#49, #73): a hidden hand card played or picked by card (`obj` =
// the session's pick of Dev's hidden hand objects, the one `action` names; it becomes `card` when chosen), or the
// spell an action's cost discards ("Mel: Empower (discard X)": `obj` = the hand card that pays it, a hidden one
// becomes `card`; the Discard choice that follows is answered with it). No card: the action as it is.
struct DevOption {
    Action action;
    ObjId obj = riftbound::no_obj;
    const riftbound::Card* card = nullptr;
    bool operator==(const DevOption&) const = default;
    // The option declares the card its cost discards (not the card it plays or picks).
    bool discards() const { return card && action.obj != obj; }
};
std::string describe(const riftbound::GameState& s, const DevOption& o);
// The panel's text for an option (#50), on the public view `pv`: a declared card by its name, a hidden card by count
// ("set aside a card"), an ability by its kind ("Mel, Defiant Soul: Empower (discard Ruthless Strike)").
std::string option_text(const riftbound::GameState& pv, const DevOption& o);
// play / activate / move / pass / choose: how the panel groups the buttons.
std::string_view option_group(const DevOption& o);

// What Dev's options are computed from: Buddy's seat, the binds that keep their card, the hidden hand cards Dev
// named (no longer hidden), Dev's decklist (the candidates' order).
struct DevContext {
    PlayerId buddy = 0;
    std::span<const ObjId> keep;
    const std::set<ObjId>* named = nullptr;
    const riftbound::Deck* dev_deck = nullptr;
};
// A hand card of Dev's Buddy doesn't know and Dev hasn't named.
bool dev_hidden(const riftbound::GameState& s, PlayerId buddy, const std::set<ObjId>& named, ObjId obj);
// The cards Dev could still name (#49): Dev's main deck and hand (runes: rune deck) minus what Buddy knows and the
// kept binds, one entry per card in decklist order. A function of the placeholders' multiset, not their order.
std::vector<const riftbound::Card*> dev_pool(const riftbound::GameState& s, PlayerId buddy,
                                             std::span<const ObjId> keep, const riftbound::Deck& deck, bool runes);
// Dev's options at Dev's decision on `s` (Session::dev_options(); free for the leak test, #50). Same list (and
// order) for any arrangement of Dev's placeholders.
std::vector<DevOption> dev_options(const riftbound::GameState& s, const DevContext& c);

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

// One committed action for the panel's log (#50), described on the public view after it.
struct LogLine {
    int turn = 0;
    PlayerId player = 0;
    std::string text;
    std::vector<std::string> cards;  // codes of the public cards the text names (the panel links them)
    bool operator==(const LogLine&) const = default;
};
inline constexpr std::size_t log_lines = 30;

struct SessionStats {
    long steps = 0, commands = 0, binds = 0, reapplies = 0, re_decides = 0, retries = 0, corrections = 0,
         misreads = 0, reorders = 0, extras = 0, pauses = 0, resumes = 0;
    long dev_decisions = 0, dev_forced = 0, dev_named = 0, dev_declared = 0, prompts = 0, refusals = 0;
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
    // Paused and resumable(): rescan the hand, then run the command that failed again; after a Timeout the command in
    // flight again, with its seq (the table answers a repeat without running it twice). Throws std::logic_error
    // otherwise.
    void resume();
    void concede(PlayerId p);
    // The service's clock and the table's status (#52). The command in flight got no answer in time: pause, keep it in
    // flight. The table restarted: its piles are gone, pause for good. Both do nothing without a running game.
    void table_timeout();
    void table_restarted();

    Outputs take() { return std::exchange(out_, {}); }

    Status status() const { return status_; }
    // DevToAct: what Dev may do. Applied unasked when there's one option only (no card in Dev's hand could change it).
    const std::vector<DevOption>& dev_options() const { return dev_options_; }
    const DevPrompt* prompt() const;  // DevToReveal: what Dev names
    Pause pause() const { return pause_; }
    bool resumable() const { return status_ == Status::Paused && pause_ != Pause::Desync && pause_ != Pause::TableRestart; }
    // The table command waiting for its answer (the service resends it after a restart, #52).
    const table::Command* inflight() const { return inflight_ ? &inflight_->cmd : nullptr; }
    // BuddyThinking: the open question to the AI again (the service asks again after a restart, #52).
    std::optional<AiRequest> ai_request() const;
    const std::string& pause_message() const { return pause_message_; }
    // The committed referee state (between steps: the state before the running step).
    const riftbound::GameState& state() const { return s_; }
    PlayerId buddy() const { return cfg_.buddy; }
    int table_id(ObjId obj) const;  // 0: the table has no id for it
    ObjId obj_of(int id) const;     // no_obj: not one of the engine's
    // Buddy's hand as the table believes it, bottom → top (table ids).
    const std::vector<int>& table_hand() const { return table_hand_; }
    const riftbound::Replay& replay() const { return replay_; }
    const std::deque<LogLine>& log() const { return log_; }  // the last log_lines actions, oldest first
    // What dev_options() needs besides the state (the leak test recomputes them on rearranged placeholders).
    std::vector<ObjId> kept() const { return bound_.keep(s_, cfg_.buddy); }
    const std::set<ObjId>& named() const { return named_; }
    const SessionConfig& config() const { return cfg_; }
    std::string replay_text() const;  // every replay line so far
    // After an undo (#97) the rebuilt session goes on numbering AI questions after `last`: a late answer to a
    // dropped question must not match a new one.
    void continue_ai_ids(std::uint64_t last) { ai_id_ = std::max(ai_id_, last); }
    std::uint64_t last_ai_id() const { return ai_id_; }
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
    // Buddy's action group being voiced (#50): a play, activation or move and the choices that belong to it.
    struct VoiceGroup {
        Action action;
        std::vector<ObjId> before;  // Move: Buddy's units at the destination before
        std::vector<riftbound::Target> targets;
        bool attack = false;
    };
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
    void take_dev(const DevOption& o);
    void bind_line(ObjId obj, const riftbound::Card* card);
    void send(table::Command c, Kind kind, int attempt = 0);
    void send_step_command();
    void done(const Inflight& f, const table::Event& e);
    void failed(const Inflight& f, const table::Event& e);
    void correction(const table::Event& e);
    void reveal(ObjId obj, const std::string& code);
    void commit();
    void reconcile_hand();
    void narrate(const riftbound::GameState& before, const Action& a);
    void flush_voice();
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
    Status waiting_ = Status::Acting;  // what a Timeout pause interrupted: back to it on the answer
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
    ObjId declared_ = riftbound::no_obj;  // the card Dev declared for a discard cost: picks the Discard choice (#73)
    std::vector<DevOption> dev_options_;
    riftbound::Replay replay_;
    std::deque<LogLine> log_;
    std::optional<VoiceGroup> group_;
    std::vector<std::string> lines_;
    Outputs out_;
    SessionStats stats_;
};

} // namespace buddy::game
