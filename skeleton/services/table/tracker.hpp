#pragma once
// What the table believes about Buddy's cards, and the procedures that keep that belief true: deal with a confirmed
// scan, play from the hand pile (feed, confirm, take, post-check, merge), recover by full scan passes. Design:
// docs/plans/05-riftbound.md → "5.5 design".

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "backend.hpp"
#include "buddy/table_protocol.hpp"

namespace buddy::table {

struct TrackerConfig {
    int confirm_reads = 2;  // agreeing reads that make a code
    bool post_check = true; // scan the spot after placing: catches double deals and double takes
    int max_reads = 8;      // reads of one card before it counts as unreadable
    int max_passes = 6;     // recovery scan passes before `failed hardware`
};

struct Known {
    int id = 0;
    std::string code;
    bool exhausted = false;
    bool operator==(const Known&) const = default;
};

struct Belief {
    std::vector<Known> hand;  // bottom → top
    std::array<std::vector<Known>, place_count> spots;  // by Place, spots only
    int deck = 0, runes = 0;
    std::optional<Known> held;  // a peeked card on the scan spot
};

// Ops the tracker asked for, cumulative.
struct OpCounts {
    long scans = 0, feeds = 0, takes = 0, deals = 0, passes = 0, recoveries = 0;
};

class Failure : public std::runtime_error {
public:
    Failure(FailReason reason, const std::string& what) : std::runtime_error(what), reason(reason) {}
    FailReason reason;
};

class Tracker {
public:
    Tracker(Backend& backend, TrackerConfig cfg) : backend_(backend), cfg_(cfg) {}

    // Every procedure throws Failure; the belief stays consistent with what was done before it.
    std::vector<CardRef> setup(const std::string& legend, const std::string& champion, int deck, int runes);
    std::vector<CardRef> deal(Source from, Place to, int n);
    CardRef peek();
    void peek_done(Place to);
    CardRef play(int id, Place to);
    void move(int id, Place to);
    void exhaust(const std::vector<int>& ids, bool exhausted);
    void shuffle_trash_into_deck(const std::vector<int>& order);
    CardRef token(const std::string& code, Place to);
    void remove(int id);
    void rescan_hand();

    // hand_corrected events (seq unset) produced since the last call.
    std::vector<Event> take_corrections();

    const Belief& belief() const { return belief_; }
    Pile hand_pile() const { return h_; }
    const OpCounts& counts() const { return counts_; }
    const TrackerConfig& config() const { return cfg_; }

private:
    Scan scan(Pile p);
    Scan read(Pile p);  // confirm_reads agreeing reads
    void place_held(Place to, const Known& card);
    void return_extras(Pile to);  // post-check: cards left on the spot go onto `to`
    void merge();                 // stack H onto B, B becomes H
    bool recover();               // full scan passes; false: the hand was as believed
    std::optional<std::vector<std::string>> pass();
    void adopt(const std::vector<std::string>& codes);
    std::pair<Place, std::size_t> find_spot(int id) const;  // throws Invalid

    Backend& backend_;
    TrackerConfig cfg_;
    Belief belief_;
    Pile h_ = Pile::TrayA, b_ = Pile::TrayB;
    int next_id_ = 1;
    OpCounts counts_;
    std::vector<Event> corrections_;
};

} // namespace buddy::table
