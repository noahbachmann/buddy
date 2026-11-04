#pragma once
// The physical table as the tracker drives it (5.5): primitive ops on Buddy's cards, the same for the sim now and the
// hardware later (5.7+). Design: docs/plans/05-riftbound.md → "5.5 design".
//
// Piles are hidden and ordered bottom → top: the decks, two hand trays (the hand and its buffer; which is which is
// the tracker's business) and the scan spot, where dealt and taken cards land and from where the arm places them.
// Spots (table::Place::Base …) hold cards by the id the tracker gave them.

#include <string>
#include <vector>

#include "buddy/table_protocol.hpp"

namespace buddy::table {

enum class Pile : std::uint8_t { Deck, Runes, TrayA, TrayB, Spot };
enum class End : std::uint8_t { Top, Bottom };

struct Scan {
    enum class Kind : std::uint8_t { Code, Unreadable, Empty } kind = Kind::Empty;
    std::string code;
    bool operator==(const Scan&) const = default;
};

class Backend {
public:
    virtual ~Backend() = default;

    virtual void begin_command() {}
    // A new game laid out: legend and champion in their zones (known as these ids), decks shuffled, the rest empty.
    virtual void setup(int legend_id, int champion_id) = 0;
    virtual Scan scan(Pile p) = 0;                // bottom card of p; Empty is reliable
    virtual void feed(Pile from, Pile to) = 0;    // bottom card of `from` onto the top of `to`
    virtual void take(Pile from) = 0;             // bottom card of `from` onto the spot
    virtual void deal(Pile from) = 0;             // top card of `from` onto the spot
    virtual void stack(Pile from, Pile onto) = 0; // all of `from` onto the top of `onto`, order kept
    virtual void place(Pile to, End end) = 0;     // the spot's bottom card onto a pile
    virtual void place(Place spot, int id) = 0;   // the spot's bottom card onto a spot, as `id`
    virtual void put(int id, Pile to, End end) = 0;  // a card on a spot onto a pile
    virtual void put(int id, Place spot) = 0;        // a card on a spot to another spot
    virtual void turn(int id, bool exhausted) = 0;
    // The trash into the deck; `order` = the trash ids as the deck should end up, bottom → top (empty: shuffle).
    virtual void shuffle_trash_into_deck(const std::vector<int>& order) = 0;
    virtual void token(Place spot, int id, const std::string& code) = 0;
    virtual void remove(int id) = 0;  // a token leaves the table
};

// Board spots keep a card's exhausted state when it moves between them (like the engine's board-to-board moves);
// everywhere else a card arrives ready.
inline bool keeps_exhausted(Place from, Place to) {
    const auto board = [](Place p) { return p == Place::Base || (p >= Place::Bf0 && p <= Place::Fd1); };
    return board(from) && board(to);
}

} // namespace buddy::table
