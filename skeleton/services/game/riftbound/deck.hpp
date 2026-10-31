#pragma once
// Decklists: Piltover Archive's text format, checked against the deck construction rules (Core Rules 103).
//
//   Legend:
//   1 Zed, Master of Shadows
//
//   Champion:
//   1 Zed, From the Shadows
//
//   MainDeck:
//   3 Blade Twirler
//   2 VEN-112              ← a printing code works too
//   …
//   Battlefields:
//   1 Zaun Warrens
//   …
//   Runes:
//   6 Fury Rune
//
// Names compare without case and punctuation. "Sideboard:" is read and ignored. 1v1: 3 battlefields.

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "card_db.hpp"

namespace buddy::riftbound {

struct DeckEntry {
    const Card* card = nullptr;
    int count = 0;
};

struct Deck {
    const Card* legend = nullptr;
    const Card* champion = nullptr;  // Chosen Champion: starts in the champion zone (103.3.a)
    std::vector<DeckEntry> main;     // main deck without the chosen champion's copy, in list order
    std::vector<DeckEntry> runes;
    std::vector<const Card*> battlefields;

    int main_size() const;  // with the chosen champion: ≥ 40 (103.3)
    int rune_count() const;
};

inline constexpr int min_main_size = 40;
inline constexpr int max_copies = 3;
inline constexpr int rune_deck_size = 12;
inline constexpr int max_signatures = 3;
inline constexpr int battlefield_count = 3;  // 1v1

// Every problem in a list, one line each ("line 7: unknown card …"), not just the first.
class DeckError : public std::runtime_error {
public:
    explicit DeckError(std::vector<std::string> problems);
    const std::vector<std::string>& problems() const { return problems_; }

private:
    std::vector<std::string> problems_;
};

Deck parse_deck(const CardDb& db, std::string_view text);  // throws DeckError
Deck load_deck(const CardDb& db, const std::string& path);  // throws DeckError (path in front) / std::runtime_error

} // namespace buddy::riftbound
