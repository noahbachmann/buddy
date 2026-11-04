#include "buddy/table_protocol.hpp"

#include <algorithm>
#include <array>
#include <tuple>

#include <nlohmann/json.hpp>

namespace buddy::table {

using json = nlohmann::json;

namespace {

constexpr std::array<std::string_view, place_count> kPlaces = {
    "hand", "deck_top", "deck_bottom", "runes_bottom", "base", "bf0", "bf1", "fd0", "fd1",
    "chain", "trash", "banish", "champion", "legend",
};
constexpr std::array<std::string_view, 11> kCommands = {
    "setup", "deal", "peek", "peek_done", "play", "move", "exhaust", "shuffle_trash_into_deck", "token", "remove",
    "rescan_hand",
};
constexpr std::array<std::string_view, 4> kReasons = {"not_in_hand", "unreadable", "hardware", "invalid"};
constexpr std::array<std::string_view, 3> kEvents = {"done", "failed", "hand_corrected"};

template <std::size_t N>
int find(const std::array<std::string_view, N>& names, std::string_view s) {
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == s) return static_cast<int>(i);
    }
    return -1;
}

const json& field(const json& j, const char* key) {
    if (!j.contains(key)) throw ProtocolError(std::string("\"") + key + "\" missing");
    return j[key];
}

int int_field(const json& j, const char* key) {
    const json& v = field(j, key);
    if (!v.is_number_integer()) throw ProtocolError(std::string("\"") + key + "\" must be an integer");
    return v.get<int>();
}

std::string string_field(const json& j, const char* key) {
    const json& v = field(j, key);
    if (!v.is_string()) throw ProtocolError(std::string("\"") + key + "\" must be a string");
    return v.get<std::string>();
}

std::vector<int> ints_field(const json& j, const char* key) {
    const json& v = field(j, key);
    if (!v.is_array()) throw ProtocolError(std::string("\"") + key + "\" must be an array");
    std::vector<int> out;
    for (const json& x : v) {
        if (!x.is_number_integer()) throw ProtocolError(std::string("\"") + key + "\" must hold integers");
        out.push_back(x.get<int>());
    }
    return out;
}

Place place_field(const json& j, const char* key) {
    const auto p = parse_place(string_field(j, key));
    if (!p) throw ProtocolError(std::string("unknown place in \"") + key + "\": " + j[key].get<std::string>());
    return *p;
}

json cards_json(const std::vector<CardRef>& cards) {
    json a = json::array();
    for (const auto& c : cards) a.push_back({{"id", c.id}, {"code", c.code}});
    return a;
}

std::vector<CardRef> cards_field(const json& j, const char* key) {
    const json& v = field(j, key);
    if (!v.is_array()) throw ProtocolError(std::string("\"") + key + "\" must be an array");
    std::vector<CardRef> out;
    for (const json& c : v) {
        if (!c.is_object()) throw ProtocolError(std::string("\"") + key + "\" must hold {id, code}");
        out.push_back({int_field(c, "id"), string_field(c, "code")});
    }
    return out;
}

} // namespace

std::string_view to_string(Place p) { return kPlaces.at(static_cast<std::size_t>(p)); }

std::optional<Place> parse_place(std::string_view s) {
    const int i = find(kPlaces, s);
    if (i < 0) return std::nullopt;
    return static_cast<Place>(i);
}

std::string_view to_string(Source s) { return s == Source::Deck ? "deck" : "runes"; }
std::string_view to_string(CommandKind k) { return kCommands.at(static_cast<std::size_t>(k)); }
std::string_view to_string(FailReason r) { return kReasons.at(static_cast<std::size_t>(r)); }

json to_json(const Command& c) {
    json j = {{"action", to_string(c.kind)}, {"seq", c.seq}};
    switch (c.kind) {
        case CommandKind::Setup:
            j["legend"] = c.legend;
            j["champion"] = c.champion;
            j["deck"] = c.deck;
            j["runes"] = c.runes;
            if (!c.sim_deck.empty()) j["sim_deck"] = c.sim_deck;
            if (!c.sim_runes.empty()) j["sim_runes"] = c.sim_runes;
            break;
        case CommandKind::Deal:
            j["from"] = to_string(c.from);
            j["to"] = to_string(c.to);
            j["n"] = c.n;
            break;
        case CommandKind::PeekDone:
            j["to"] = to_string(c.to);
            break;
        case CommandKind::Play:
        case CommandKind::Move:
            j["id"] = c.id;
            j["to"] = to_string(c.to);
            break;
        case CommandKind::Exhaust:
            j["ids"] = c.ids;
            j["exhausted"] = c.exhausted;
            break;
        case CommandKind::ShuffleTrashIntoDeck:
            if (!c.order.empty()) j["order"] = c.order;
            break;
        case CommandKind::Token:
            j["code"] = c.code;
            j["to"] = to_string(c.to);
            break;
        case CommandKind::Remove:
            j["id"] = c.id;
            break;
        case CommandKind::Peek:
        case CommandKind::RescanHand:
            break;
    }
    return j;
}

Command parse_command(const json& j) {
    if (!j.is_object()) throw ProtocolError("not a JSON object");
    const int k = find(kCommands, string_field(j, "action"));
    if (k < 0) throw ProtocolError("unknown action " + j["action"].get<std::string>());
    Command c;
    c.kind = static_cast<CommandKind>(k);
    const json& seq = field(j, "seq");
    if (!seq.is_number_integer()) throw ProtocolError("\"seq\" must be an integer");
    c.seq = seq.get<std::int64_t>();
    switch (c.kind) {
        case CommandKind::Setup:
            c.legend = string_field(j, "legend");
            c.champion = string_field(j, "champion");
            c.deck = int_field(j, "deck");
            c.runes = int_field(j, "runes");
            if (c.deck < 0 || c.runes < 0) throw ProtocolError("negative pile size");
            for (const auto& [key, list, n] : {std::tuple{"sim_deck", &c.sim_deck, c.deck},
                                                std::tuple{"sim_runes", &c.sim_runes, c.runes}}) {
                if (!j.contains(key)) continue;
                const json& v = j[key];
                if (!v.is_array() || !std::all_of(v.begin(), v.end(), [](const json& x) { return x.is_string(); }))
                    throw ProtocolError(std::string("\"") + key + "\" must hold codes");
                *list = v.get<std::vector<std::string>>();
                if (static_cast<int>(list->size()) != n) throw ProtocolError(std::string("\"") + key + "\" isn't the pile's size");
            }
            break;
        case CommandKind::Deal: {
            const std::string from = string_field(j, "from");
            if (from != "deck" && from != "runes") throw ProtocolError("\"from\" must be deck or runes");
            c.from = from == "deck" ? Source::Deck : Source::Runes;
            c.to = place_field(j, "to");
            c.n = int_field(j, "n");
            if (c.n < 1 || c.n > 60) throw ProtocolError("\"n\" out of range");
            break;
        }
        case CommandKind::PeekDone:
            c.to = place_field(j, "to");
            break;
        case CommandKind::Play:
        case CommandKind::Move:
            c.id = int_field(j, "id");
            c.to = place_field(j, "to");
            break;
        case CommandKind::Exhaust: {
            c.ids = ints_field(j, "ids");
            const json& e = field(j, "exhausted");
            if (!e.is_boolean()) throw ProtocolError("\"exhausted\" must be true or false");
            c.exhausted = e.get<bool>();
            break;
        }
        case CommandKind::ShuffleTrashIntoDeck:
            if (j.contains("order")) c.order = ints_field(j, "order");
            break;
        case CommandKind::Token:
            c.code = string_field(j, "code");
            c.to = place_field(j, "to");
            if (!is_spot(c.to)) throw ProtocolError("a token goes to a spot");
            break;
        case CommandKind::Remove:
            c.id = int_field(j, "id");
            break;
        case CommandKind::Peek:
        case CommandKind::RescanHand:
            break;
    }
    return c;
}

Command parse_payload(std::string_view payload) {
    const json j = json::parse(payload, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) throw ProtocolError("not JSON");
    return parse_command(j);
}

json to_json(const Event& e) {
    json j = {{"event", kEvents.at(static_cast<std::size_t>(e.kind))}};
    switch (e.kind) {
        case EventKind::Done:
            j["seq"] = e.seq;
            j["cards"] = cards_json(e.cards);
            break;
        case EventKind::Failed:
            j["seq"] = e.seq;
            j["reason"] = to_string(e.reason);
            j["message"] = e.message;
            break;
        case EventKind::HandCorrected:
            j["seq"] = e.seq;
            j["hand"] = cards_json(e.hand);
            j["replaced"] = cards_json(e.replaced);
            j["extra"] = cards_json(e.extra);
            j["missing"] = e.missing;
            break;
    }
    return j;
}

Event parse_event(const json& j) {
    if (!j.is_object()) throw ProtocolError("not a JSON object");
    const int k = find(kEvents, string_field(j, "event"));
    if (k < 0) throw ProtocolError("unknown event " + j["event"].get<std::string>());
    Event e;
    e.kind = static_cast<EventKind>(k);
    const json& seq = field(j, "seq");
    if (!seq.is_number_integer()) throw ProtocolError("\"seq\" must be an integer");
    e.seq = seq.get<std::int64_t>();
    switch (e.kind) {
        case EventKind::Done:
            e.cards = cards_field(j, "cards");
            break;
        case EventKind::Failed: {
            const int r = find(kReasons, string_field(j, "reason"));
            if (r < 0) throw ProtocolError("unknown reason");
            e.reason = static_cast<FailReason>(r);
            if (j.contains("message")) e.message = string_field(j, "message");
            break;
        }
        case EventKind::HandCorrected:
            e.hand = cards_field(j, "hand");
            e.replaced = cards_field(j, "replaced");
            e.extra = cards_field(j, "extra");
            e.missing = ints_field(j, "missing");
            break;
    }
    return e;
}

} // namespace buddy::table
