#pragma once
// Card data: printings (what a physical card is, by its code) grouped into cards (what the rules know, by name).
// Source: ~/buddy/data/riftbound/cards.json from deploy/pi/fetch-cards.sh (Riftcodex, one entry per printing).
//
// A card's identity is its name (Core Rules 103.3.b: copies are counted by name), reprints and alternate arts are
// printings of it. Riftcodex spells some names differently per printing: legends "Vi - Piltover Enforcer", variants
// "… (Alternate Art)", "… (Signature)", "… (Metal)"; card_name() undoes that. Printings of one card also disagree on
// region tags and reminder text, so tags are merged and the text comes from the card's home printing. Type, domains
// and stats must agree; a card where they don't is kept but marked (Card::conflict), and decks refuse it.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "code.hpp"

namespace buddy::riftbound {

class CardRules;  // rules.hpp

enum class CardType { Legend, Unit, Spell, Gear, Rune, Battlefield };
std::string_view to_string(CardType type);

// Bit set of domains; 0 = colorless (fits every deck).
using Domains = std::uint8_t;
namespace domain {
inline constexpr Domains fury = 1, calm = 2, mind = 4, body = 8, chaos = 16, order = 32;
}
std::string domains_string(Domains d);  // "Fury+Order", "Colorless"

// When a card may be played beyond its own Neutral Open main phase: Action 806 (showdowns too), Reaction 813 (closed
// states too). Printed as the card's leading keyword; granted ones ("Quick-Draw") come from its rules (5.3).
enum class Timing : std::uint8_t { Normal, Action, Reaction };
std::string_view to_string(Timing timing);
// "[Reaction] (Play any time …)…" → Reaction. A keyword followed by "[>]" belongs to an ability, not the card.
Timing printed_timing(std::string_view text);

struct Printing {
    Code code;               // total = the "/298" printed on it, if Riftcodex has one
    std::string label;       // code.str(), plus "/total" where promos share it: "OPP-009/221" (Serrated Dirk)
    std::string set_name;    // "Origins"
    std::string full_name;   // as Riftcodex has it: "Vi - Piltover Enforcer (Signature)"
};

struct Card {
    std::string name;                 // "Vi, Piltover Enforcer"
    CardType type = CardType::Unit;
    bool champion = false;            // champion unit: can be a Chosen Champion (103.3.a)
    bool signature = false;           // signature card (103.3.d)
    bool token = false;               // never in a deck
    Domains domains = 0;
    std::optional<int> energy, might, power;  // cost: energy + power (runes of the card's domain); might = strength
    Timing timing = Timing::Normal;   // from text
    std::vector<std::string> tags;    // all printings' tags: champion tag ("Vi"), regions, kinds
    std::string text;                 // rules text of the home printing, Riftcodex markup (":rb_might:", "[Shield 2]")
    std::vector<Printing> printings;  // home printing first
    std::string conflict;             // non-empty: printings disagree on type/domains/stats (what differs)
    const CardRules* rules = nullptr; // what the engine runs; nullptr = not implemented (D21). Static, never freed

    const Printing& home() const { return printings.front(); }
    bool has_tag(std::string_view tag) const;
};

// Name as decks and rules know it: variant suffix dropped, legends' " - " → ", ".
std::string card_name(std::string_view riftcodex_name);

class CardDb {
public:
    static CardDb load(const std::string& path);  // throws std::runtime_error naming the file
    static CardDb parse(const nlohmann::json& doc);

    struct Found {
        const Card* card = nullptr;          // nullptr: error says why
        const Printing* printing = nullptr;
        std::string error;
    };
    // A scanned or typed code: "OGN • 007a/298" → printing + card. Errors: not a code, unknown code, wrong set size,
    // ambiguous (OPP promos reuse numbers: OPP-009/219 and OPP-009/221 are different cards, the total decides).
    Found find_code(std::string_view text) const;
    // Exact name (case and punctuation don't count: names::squash); nullptr if none.
    const Card* find_name(std::string_view name) const;
    // Names close to a misspelled one, best first (for "did you mean").
    std::vector<std::string> similar_names(std::string_view name, std::size_t max = 3) const;

    const std::vector<Card>& cards() const { return cards_; }
    std::size_t printing_count() const { return by_code_.size(); }
    const std::vector<std::string>& warnings() const { return warnings_; }  // skipped printings
    const std::string& fetched() const { return fetched_; }

private:
    std::vector<Card> cards_;                               // sorted by name
    std::map<std::string, std::size_t, std::less<>> by_name_;  // names::squash → index
    std::multimap<std::string, std::pair<std::size_t, std::size_t>, std::less<>> by_code_;  // Code::str → card, printing
    std::vector<std::string> warnings_;
    std::string fetched_;
};

} // namespace buddy::riftbound
