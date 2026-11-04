#pragma once
// game ⇄ table (Riftbound 5.5, docs/plans/05-riftbound.md → "5.5 design"): commands on buddy/table/cmd, events on
// buddy/table/event. Every command carries a `seq` and ends with exactly one `done` or `failed` event for that seq;
// a `hand_corrected` may come before it. Cards are `{id, code}`: ids are the table's, codes printed codes.

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace buddy::table {

// Where a card can go. Piles: hand (top of the hand pile), deck top/bottom, rune deck bottom. Spots (face up or
// facedown cards the table knows by id): the rest.
enum class Place : std::uint8_t {
    Hand, DeckTop, DeckBottom, RunesBottom,
    Base, Bf0, Bf1, Fd0, Fd1, Chain, Trash, Banish, Champion, Legend,
};
inline constexpr int place_count = 14;
std::string_view to_string(Place p);
std::optional<Place> parse_place(std::string_view s);
inline bool is_spot(Place p) { return p >= Place::Base; }

enum class Source : std::uint8_t { Deck, Runes };
std::string_view to_string(Source s);

struct CardRef {
    int id = 0;
    std::string code;
    bool operator==(const CardRef&) const = default;
};

enum class CommandKind : std::uint8_t {
    Setup,      // legend, champion (codes), deck, runes (counts), [sim_deck, sim_runes]: a new game, all else forgotten
    Deal,       // n cards from the top of `from` to `to`, each scanned: done.cards = the dealt cards
    Peek,       // top card of the deck to the scan spot: done.cards = that card (Vision)
    PeekDone,   // the peeked card to `to` (deck_top / deck_bottom)
    Play,       // hand card `id` to `to`
    Move,       // a card on a spot `id` to `to` (hand = return to hand, deck_bottom = recycle)
    Exhaust,    // `ids` exhausted or readied
    ShuffleTrashIntoDeck,  // Burn Out (432.2.b); `order` = the resulting deck bottom → top if the caller knows it (sim)
    Token,      // a token `code` appears at `to`: done.cards = it
    Remove,     // token `id` stops existing
    RescanHand, // full scan of the hand: hand_corrected if it differs
};
std::string_view to_string(CommandKind k);

struct Command {
    CommandKind kind = CommandKind::RescanHand;
    std::int64_t seq = 0;
    Source from = Source::Deck;  // Deal
    Place to = Place::Hand;      // Deal, PeekDone, Play, Move, Token
    int n = 1;                   // Deal
    int id = 0;                  // Play, Move, Remove
    std::vector<int> ids{};        // Exhaust
    bool exhausted = true;       // Exhaust
    std::string code{};          // Token
    std::string legend{}, champion{};  // Setup
    int deck = 0, runes = 0;       // Setup
    std::vector<int> order{};      // ShuffleTrashIntoDeck, optional
    // Setup, sim backend only (optional): the decks' codes, bottom → top before the sim shuffles them; game sends its
    // decklist's home printings. The hardware ignores them.
    std::vector<std::string> sim_deck{}, sim_runes{};
    bool operator==(const Command&) const = default;
};

class ProtocolError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

nlohmann::json to_json(const Command& c);
Command parse_command(const nlohmann::json& j);  // throws ProtocolError naming the field
Command parse_payload(std::string_view payload);  // MQTT payload; ProtocolError also for bad JSON

enum class EventKind : std::uint8_t { Done, Failed, HandCorrected };
enum class FailReason : std::uint8_t {
    NotInHand,   // the card isn't in the hand after a recovery
    Unreadable,  // a card couldn't be read
    Hardware,    // piles that don't add up: scans never agree, a pile ran empty, …
    Invalid,     // a command the table can't run: unknown id, a card that isn't where the command says
};
std::string_view to_string(FailReason r);

struct Event {
    EventKind kind = EventKind::Done;
    std::int64_t seq = 0;
    std::vector<CardRef> cards{};  // Done
    FailReason reason = FailReason::Hardware;  // Failed
    std::string message{};                     // Failed: for logs
    // HandCorrected: the hand after the correction (bottom → top), cards whose code was wrong (now right), cards
    // nobody knew of (new ids), ids that aren't there.
    std::vector<CardRef> hand{}, replaced{}, extra{};
    std::vector<int> missing{};
    bool operator==(const Event&) const = default;
};

nlohmann::json to_json(const Event& e);
Event parse_event(const nlohmann::json& j);  // throws ProtocolError

} // namespace buddy::table
