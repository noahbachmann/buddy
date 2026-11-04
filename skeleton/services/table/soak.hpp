#pragma once
// The soak (5.5, #43): a workload of engine games (buddy-game --table-workload) played against the sim, with table
// standing in for game: objects map to the ids the table reports, hand_corrected is adopted (extra cards back on the
// deck top), a failed play or deal is retried once. Counts what wasn't recovered and what the table paid per play.

#include <cstdint>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "sim.hpp"
#include "table.hpp"

namespace buddy::table {

struct SoakOptions {
    SimFaults faults;
    TrackerConfig tracker;
    std::uint64_t seed = 0;  // sim seed = seed + game number
};

struct GameStats {
    std::vector<std::string> unrecovered;  // wrong card played, failed, belief ≠ truth
    long commands = 0, plays = 0, retries = 0, corrections = 0, replaced = 0, extra = 0, missing = 0;
    std::vector<int> feeds, scans;  // per play
    OpCounts ops;
    FaultCounts faults;
};

// One game: setup, then command lines, then finish().
class StandIn {
public:
    StandIn(const nlohmann::json& setup, const SoakOptions& options, std::uint64_t game);

    void apply(const nlohmann::json& line);  // {"cmd": …, "objs": […]}
    GameStats finish();                      // rescan_hand, then belief = truth?

    const Sim& sim() const { return *sim_; }
    const Table& table() const { return *table_; }
    int id_of(int obj) const;  // -1: no id
    const std::vector<std::string>& unrecovered_so_far() const { return stats_.unrecovered; }

private:
    std::vector<Event> run(Command c);
    void unrecovered(const std::string& what);
    void check_after_recovery(const std::string& what, long recoveries_before);

    std::unique_ptr<Sim> sim_;
    std::unique_ptr<Table> table_;
    std::map<int, int> id_;           // engine object → table id
    std::map<int, std::string> code_; // engine object → code (the truth: the engine's card)
    std::int64_t seq_ = 0;
    GameStats stats_;
};

// Reads workload lines from `in` until EOF, plays every game, prints the report to `out`. Returns the number of
// unrecovered states.
struct SoakReport {
    int games = 0;
    long unrecovered = 0;
    std::vector<GameStats> per_game;
};
SoakReport soak(std::istream& in, const SoakOptions& options, std::ostream* log = nullptr);
void print_report(const SoakReport& r, const SoakOptions& options, std::ostream& out);

} // namespace buddy::table
