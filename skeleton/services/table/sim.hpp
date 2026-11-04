#pragma once
// The simulated table (5.5): the true piles and spots of Buddy's cards behind the Backend interface, with seeded
// faults per op (scans, feeds, takes, deals) and per command (a disturbed hand pile). Tests and the soak compare the
// tracker's belief with this truth. Design: docs/plans/05-riftbound.md → "5.5 design".

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "backend.hpp"
#include "tracker.hpp"

namespace buddy::table {

// Probability per op (disturbed: per command).
struct SimFaults {
    double misread = 0;      // a scan reads a wrong code (one of the sim's codes)
    double unreadable = 0;   // a scan reads nothing
    double double_feed = 0;  // a feed moves two cards
    double missed_feed = 0;  // a feed moves none
    double double_take = 0;  // a take moves two cards onto the spot
    double double_deal = 0;  // a deal moves two cards onto the spot
    double disturbed = 0;    // two hand cards swap before a command
    bool operator==(const SimFaults&) const = default;
};
// "misread=0.02,unreadable=0.05,…", "stress" (the 5.5 stress rates), "none" or "". Throws std::invalid_argument.
SimFaults parse_faults(std::string_view text);
std::string to_string(const SimFaults& f);

// What the game starts with, bottom → top. shuffle: the decks are shuffled at setup (5.6), else dealt in this order
// (the soak plays the engine's order).
struct SimSetup {
    std::vector<std::string> deck, runes;
    std::string legend = "legend", champion = "champion";
    bool shuffle = false;
};

struct Phys {
    int serial = 0;  // the physical card, for the sim alone
    std::string code;
    int id = 0;      // on a spot: the tracker's id
    bool exhausted = false;
};

struct FaultCounts {
    long misread = 0, unreadable = 0, double_feed = 0, missed_feed = 0, double_take = 0, double_deal = 0,
         disturbed = 0;
    long total() const {
        return misread + unreadable + double_feed + missed_feed + double_take + double_deal + disturbed;
    }
};

class Sim : public Backend {
public:
    Sim(SimSetup setup, SimFaults faults, std::uint64_t seed);
    // What the next setup() lays out (the service: from each setup command).
    void configure(SimSetup setup);

    void begin_command() override;
    void setup(int legend_id, int champion_id) override;
    Scan scan(Pile p) override;
    void feed(Pile from, Pile to) override;
    void take(Pile from) override;
    void deal(Pile from) override;
    void stack(Pile from, Pile onto) override;
    void place(Pile to, End end) override;
    void place(Place spot, int id) override;
    void put(int id, Pile to, End end) override;
    void put(int id, Place spot) override;
    void turn(int id, bool exhausted) override;
    void shuffle_trash_into_deck(const std::vector<int>& order) override;
    void token(Place spot, int id, const std::string& code) override;
    void remove(int id) override;

    // Truth.
    const std::vector<Phys>& pile(Pile p) const { return piles_[static_cast<std::size_t>(p)]; }
    const std::vector<Phys>& spot(Place p) const { return spots_[static_cast<std::size_t>(p)]; }
    // Codes of the cards the spot gave away during the current command, in order, except onto the hand trays: in a
    // play, the first is the card that left the hand.
    const std::vector<std::string>& placed() const { return placed_; }
    // "" if the belief is the truth: hand order by code, deck and rune counts, every spot's ids, codes and exhausted.
    // hand_only: the hand piles and the scan spot alone (what a recovery fixes).
    std::string compare(const Belief& b, Pile hand, bool hand_only = false) const;
    const FaultCounts& faults() const { return counts_; }
    SimFaults& rates() { return rates_; }

private:
    bool chance(double p);
    std::vector<Phys>& at(Pile p) { return piles_[static_cast<std::size_t>(p)]; }
    std::vector<Phys>& at(Place p) { return spots_[static_cast<std::size_t>(p)]; }
    Phys take_id(int id);  // off its spot; throws std::logic_error if not on one
    void move_bottom(Pile from, Pile to, int n);

    SimSetup setup_;
    SimFaults rates_;
    std::mt19937_64 rng_;
    std::array<std::vector<Phys>, 5> piles_;
    std::array<std::vector<Phys>, place_count> spots_;
    std::vector<std::string> codes_;  // every code the sim knows: misreads pick from these
    std::vector<std::string> placed_;
    FaultCounts counts_;
    int serial_ = 0;
};

} // namespace buddy::table
