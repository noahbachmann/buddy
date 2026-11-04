#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "buddy/table_protocol.hpp"

using namespace buddy::table;
using json = nlohmann::json;

namespace {

std::vector<Command> every_command() {
    std::vector<Command> out;
    Command c;
    c.seq = 1;
    c.kind = CommandKind::Setup;
    c.legend = "OGN-261";
    c.champion = "OGN-262";
    c.deck = 40;
    c.runes = 12;
    out.push_back(c);
    c.deck = 2;
    c.runes = 1;
    c.sim_deck = {"OGN-001", "OGN-002"};
    c.sim_runes = {"OGN-R01"};
    out.push_back(c);
    c = {.kind = CommandKind::Deal, .seq = 2, .from = Source::Runes, .to = Place::Base, .n = 2};
    out.push_back(c);
    out.push_back({.kind = CommandKind::Peek, .seq = 3});
    out.push_back({.kind = CommandKind::PeekDone, .seq = 4, .to = Place::DeckBottom});
    out.push_back({.kind = CommandKind::Play, .seq = 5, .to = Place::Bf1, .id = 7});
    out.push_back({.kind = CommandKind::Move, .seq = 6, .to = Place::Hand, .id = 8});
    out.push_back({.kind = CommandKind::Exhaust, .seq = 7, .ids = {1, 2}, .exhausted = false});
    out.push_back({.kind = CommandKind::ShuffleTrashIntoDeck, .seq = 8, .order = {3, 1, 2}});
    out.push_back({.kind = CommandKind::ShuffleTrashIntoDeck, .seq = 9});
    out.push_back({.kind = CommandKind::Token, .seq = 10, .to = Place::Base, .code = "token:Bird"});
    out.push_back({.kind = CommandKind::Remove, .seq = 11, .id = 9});
    out.push_back({.kind = CommandKind::RescanHand, .seq = 12});
    return out;
}

} // namespace

TEST(TableProtocol, CommandsRoundTrip) {
    for (const Command& c : every_command()) {
        const json j = to_json(c);
        EXPECT_EQ(parse_command(j), c) << j.dump();
        EXPECT_EQ(parse_payload(j.dump()), c);
    }
}

TEST(TableProtocol, EventsRoundTrip) {
    const std::vector<Event> events = {
        {.kind = EventKind::Done, .seq = 4, .cards = {{1, "OGN-001"}, {2, "OGN-002"}}},
        {.kind = EventKind::Done, .seq = 5},
        {.kind = EventKind::Failed, .seq = 6, .reason = FailReason::NotInHand, .message = "gone"},
        {.kind = EventKind::HandCorrected, .seq = 7, .hand = {{1, "A"}, {3, "C"}}, .replaced = {{1, "A"}},
         .extra = {{3, "C"}}, .missing = {2}},
    };
    for (const Event& e : events) EXPECT_EQ(parse_event(to_json(e)), e) << to_json(e).dump();
}

TEST(TableProtocol, PlacesByName) {
    for (int i = 0; i < place_count; ++i) {
        const auto p = static_cast<Place>(i);
        EXPECT_EQ(parse_place(to_string(p)), p);
    }
    EXPECT_FALSE(parse_place("bf2"));
    EXPECT_TRUE(is_spot(Place::Base));
    EXPECT_FALSE(is_spot(Place::DeckBottom));
}

TEST(TableProtocol, BadCommandsSayWhy) {
    const auto error = [](const std::string& payload) {
        try {
            parse_payload(payload);
        } catch (const ProtocolError& e) {
            return std::string(e.what());
        }
        return std::string("accepted");
    };
    EXPECT_EQ(error("nope"), "not JSON");
    EXPECT_EQ(error(R"({"seq": 1})"), "\"action\" missing");
    EXPECT_EQ(error(R"({"action": "dance", "seq": 1})"), "unknown action dance");
    EXPECT_EQ(error(R"({"action": "play", "id": 1, "to": "base"})"), "\"seq\" missing");
    EXPECT_EQ(error(R"({"action": "play", "seq": 1, "id": "x", "to": "base"})"), "\"id\" must be an integer");
    EXPECT_EQ(error(R"({"action": "play", "seq": 1, "id": 1, "to": "moon"})"), "unknown place in \"to\": moon");
    EXPECT_EQ(error(R"({"action": "deal", "seq": 1, "from": "trash", "to": "hand", "n": 1})"),
              "\"from\" must be deck or runes");
    EXPECT_EQ(error(R"({"action": "deal", "seq": 1, "from": "deck", "to": "hand", "n": 0})"), "\"n\" out of range");
    EXPECT_EQ(error(R"({"action": "token", "seq": 1, "code": "x", "to": "hand"})"), "a token goes to a spot");
    EXPECT_EQ(error(R"({"action": "setup", "seq": 1, "legend": "L", "champion": "C", "deck": 2, "runes": 0,
                        "sim_deck": ["A"]})"), "\"sim_deck\" isn't the pile's size");
}
