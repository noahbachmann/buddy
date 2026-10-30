// tests/resources-dev.json: Dev's bridge (GET /clip/v2/resource, 2026-10-30), trimmed to the types Home reads, ids
// made readable. Room "Home" (Bedroom Ceiling: colour; Kitchen Ceiling, Living Room Corner: dimmable only; a dimmer
// switch), zones "Bedroom" and "Living Room" (Kitchen Ceiling + Living Room Corner), "Hue color lamp 2"/"3" in no room
// (bridge_home lists their devices and the bridge itself). Scenes per group, "Relax", "Read", … in several.

#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include "home.hpp"
#include "lights.hpp"

using namespace buddy::hue;
using nlohmann::json;

namespace {

Home dev_home() {
    std::ifstream in(BUDDY_SOURCE_DIR "/services/hue/tests/resources-dev.json");
    Home home;
    home.load(json::parse(in)["data"]);
    return home;
}

std::string found(const Home& home, const std::string& spoken) {
    try {
        const auto t = home.find(spoken);
        if (!t) return "-";
        return (t->group ? t->group->kind + " " : "light ") + t->name();
    } catch (const Ambiguous& e) {
        return std::string("ambiguous ") + e.what();
    }
}

} // namespace

TEST(DevHome, Load) {
    const Home home = dev_home();
    const json state = home.state();
    std::vector<std::string> rooms;
    for (const auto& r : state["rooms"]) rooms.push_back(r["kind"].get<std::string>() + " " + r["name"].get<std::string>());
    EXPECT_EQ(rooms, (std::vector<std::string>{"home All lights", "room Home", "zone Bedroom", "zone Living Room"}));
    EXPECT_EQ(state["rooms"][0]["lights"].size(), 5u);  // lamps in no room too
    EXPECT_EQ(state["rooms"][1]["lights"].size(), 3u);  // dimmer switch has no light
    EXPECT_EQ(state["lights"].size(), 5u);
}

TEST(DevHome, FindBySpokenName) {
    const Home home = dev_home();
    EXPECT_EQ(found(home, "bedroom"), "zone Bedroom");  // not "Bedroom Ceiling"
    EXPECT_EQ(found(home, "living room"), "zone Living Room");
    EXPECT_EQ(found(home, "kitchen"), "light Kitchen Ceiling");
    EXPECT_EQ(found(home, "corner"), "light Living Room Corner");
    EXPECT_EQ(found(home, "bedroom ceiling"), "light Bedroom Ceiling");
    EXPECT_EQ(found(home, "lamp two"), "light Hue color lamp 2");
    EXPECT_EQ(found(home, "all"), "home All lights");
    EXPECT_EQ(found(home, "home"), "room Home");  // its own name beats the every-light word
    EXPECT_EQ(found(home, "ceiling"), "ambiguous Bedroom Ceiling / Kitchen Ceiling");
}

TEST(DevHome, Replies) {
    const Home home = dev_home();
    auto say = [&](std::string action, std::string query, std::string device) {
        return plan({.action = std::move(action), .query = std::move(query), .device = std::move(device)}, home, {}).say;
    };
    EXPECT_EQ(say("color", "warm white", "kitchen"), "The Kitchen Ceiling can't change its white.");
    EXPECT_EQ(say("color", "blue", "living room"), "The Living Room lights can't change colour.");
    EXPECT_EQ(say("scene", "relax", ""), "Which one, Relax in Home or Relax in Living Room?");
    EXPECT_EQ(say("scene", "relax", "home"), "");  // the answer to that works
    EXPECT_EQ(say("scene", "relax", "bedroom"), "I don't know a scene called relax in Bedroom.");
}

// Every light line in commands.tsv works on Dev's lights: names resolve, scenes exist, lights can do it.
TEST(DevHome, CorpusLightCommands) {
    const Home home = dev_home();
    std::ifstream in(BUDDY_SOURCE_DIR "/tools/voice/commands.tsv");
    const std::vector<std::string> hue = {"on", "off", "toggle", "brightness", "brighter", "darker",
                                          "color", "temperature", "scene", "status"};
    int checked = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        std::stringstream ss(line);
        for (std::string c; std::getline(ss, c, '\t');) cols.push_back(c);
        cols.resize(5);
        if (std::find(hue.begin(), hue.end(), cols[2]) == hue.end()) continue;
        const Plan p = plan({.action = cols[2], .query = cols[3], .device = cols[4]}, home, {});
        EXPECT_TRUE(p.ok) << cols[1] << " → " << p.say;
        ++checked;
    }
    EXPECT_GT(checked, 20);
}
