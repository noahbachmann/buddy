#pragma once
// Session games on a simulated table (5.6.5, #51): a Session with Buddy's side on an in-process table_core Table +
// Sim and Dev a SimDev, run the way the service runs it, with the checks after every step. The session tests, the
// soak (`buddy-game --sim --games N`) and its ctest share it. Lives in game_sim (game_core + table_core): the
// service's game_core stays free of the sim table.
//
// Checked after every committed step: the engine's view of Buddy's cards = the table's belief (hand order, ids, codes,
// spots, exhausted, counts) = the sim's truth (the hand as a multiset: a disturbed pile stays unnoticed until a play
// mismatches, 5.5); every card of Dev's that Buddy knows is SimDev's true one and came from a prompt or Dev's choice.
// Sampled: the leak check (#50: the payload and Dev's options are the same for every arrangement of the hidden cards).
// At the end: a rescan, then the belief = the truth with the order; the replay reproduces the state; the game with
// Dev's true cards from the start replays legally into the same state.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "riftbound/ai/player.hpp"
#include "riftbound/card_db.hpp"
#include "riftbound/deck.hpp"
#include "riftbound/state.hpp"
#include "session.hpp"
#include "sim.hpp"
#include "sim_dev.hpp"
#include "table.hpp"

namespace buddy::game {

struct SimGameConfig {
    std::array<riftbound::Deck, riftbound::player_count> decks;
    std::array<std::string, riftbound::player_count> deck_texts;
    PlayerId buddy = 0;
    std::uint64_t seed = 0;
    table::SimFaults faults;
    std::uint64_t sim_seed = 1;
    bool true_deck = false;  // SimDev's cards: a true shuffled deck (false: the placeholders are the truth)
    // Null: greedy, seeded from `seed` (the #48–#50 tests' games).
    std::unique_ptr<riftbound::ai::Player> buddy_player, dev_player;
    // Wraps the sim for the table (tests: a backend that forces failures); unset: the sim itself.
    std::function<std::unique_ptr<table::Backend>(table::Sim&)> wrap;
};

class SimGame {
public:
    SimGame(const riftbound::CardDb& db, SimGameConfig cfg);

    const riftbound::CardDb& db;
    SessionConfig cfg;
    table::Sim sim;
    std::unique_ptr<table::Backend> wrapper;
    table::Table table;
    std::unique_ptr<riftbound::ai::Player> buddy_ai;
    SimDev dev;
    std::unique_ptr<Session> session;

    bool check_candidates = false;  // at Dev's priority: the offered cards are exactly the possible ones
    bool check_truth = true;        // Dev's cards Buddy knows = SimDev's truth (off: a test answers for Dev)
    bool check_counts = true;
    int leak_every = 0;             // the leak check before every n-th input (0: off)
    int resumes_left = 20;          // Dev presses resume after a table pause
    std::function<void(const table::Command&)> before_cmd;
    std::function<void(const table::Command&, const std::vector<table::Event>&)> after_cmd;
    std::function<std::optional<riftbound::Action>(const AiRequest&)> buddy_choice;
    std::function<bool()> stop_when;  // checked between inputs: true → run() returns

    std::vector<std::string> problems;  // a check failed: unrecovered
    std::vector<std::string> leaks;     // the leak check failed
    std::vector<std::string> voice;
    std::vector<Pause> pauses;
    long checked = 0, leak_checks = 0, prompt_cards = 0;
    long reanswers = 0;  // SimDev's refused prompt answers (SimDev::answer): the session's refusals count them too
    int collapsed = 0;  // true_replay_problem: Dev's choices the placeholders turned into a prompt

    PlayerId dev_seat() const { return 1 - session->buddy(); }
    // Runs until the game is over, a pause Dev doesn't resume, or stop_when.
    void run();
    // The checks at the end: "" = fine.
    std::string final_check();               // a rescan of the hand, then the belief is the truth, order included
    std::string replay_problem() const;      // the session's replay reproduces its state
    std::string true_replay_problem();       // the game with Dev's true cards from the start is the same legal game
    // The payload and Dev's options on `arrangements` rearrangements of Dev's placeholders and Buddy's hidden cards.
    std::string leak_problem(riftbound::Rng& rng, int arrangements = 2);

private:
    bool buddy_knows(const riftbound::GameState& s, ObjId id) const;
    void execute(const table::Command& c);
    void check();
    std::string compare() const;
    std::string dev_problem();
    std::string candidates_problem() const;

    std::set<ObjId> announced_;              // Dev's cards prompted or named since the last committed step
    std::vector<std::uint8_t> known_before_;  // Dev's objects: Buddy's known bit at the last committed step
    long last_steps_ = 0, inputs_ = 0;
    riftbound::Rng leak_rng_;
};

// `s` with the cards of `owner`'s hidden objects (hand, main deck, rune deck) that `knower` doesn't know shuffled
// among themselves (runes with runes), except `fixed`: an arrangement the knower can't tell apart (#50's leak check).
riftbound::GameState rearranged(const riftbound::GameState& s, PlayerId owner, PlayerId knower,
                                const std::set<ObjId>& fixed, riftbound::Rng& rng);
// Dev's placeholders the session may rearrange: every hidden one except the kept binds and the named cards.
std::set<ObjId> dev_fixed(const Session& session);

} // namespace buddy::game
