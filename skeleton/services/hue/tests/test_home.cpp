#include <gtest/gtest.h>

#include "fixture.hpp"

using namespace buddy::hue;
using namespace buddy::hue::test;
using nlohmann::json;

namespace {

std::string found(const Home& home, const std::string& spoken) {
    const auto t = home.find(spoken);
    if (!t) return "-";
    return (t->group ? t->group->kind + " " : "light ") + t->name();
}

json light_state(const Home& home, const std::string& id) {
    const json state = home.state();  // not a temporary: the loop outlives the full expression
    for (const auto& l : state["lights"])
        if (l["id"] == id) return l;
    return nullptr;
}

} // namespace

TEST(Home, Load) {
    const Home home = loaded_home();
    const json state = home.state();
    std::vector<std::string> rooms;
    for (const auto& r : state["rooms"]) rooms.push_back(r["kind"].get<std::string>() + " " + r["name"].get<std::string>());
    // home first, rooms, zones; Garage has no grouped_light (no lights): left out
    EXPECT_EQ(rooms, (std::vector<std::string>{"home All lights", "room Bedroom", "room Living room", "zone Reading"}));
    EXPECT_EQ(state["rooms"][2]["lights"], (json{"ceiling", "floor", "desk"}));
    EXPECT_EQ(state["rooms"][2]["brightness"], 61);
    EXPECT_EQ(state["rooms"][3]["lights"], (json{"floor", "desk"}));
    EXPECT_EQ(state["rooms"][1]["scenes"].size(), 2u);
    EXPECT_EQ(state["rooms"][0]["lights"].size(), 5u);

    const json floor = light_state(home, "floor");
    EXPECT_EQ(floor["room"], "living");  // the room, not the zone
    EXPECT_EQ(floor["color"], true);
    EXPECT_EQ(floor["mirek"], 366);
    EXPECT_EQ(floor["xy"][0], 0.4573);
    const json plug = light_state(home, "plug");
    EXPECT_EQ(plug["dimming"], false);
    EXPECT_EQ(plug["temperature"], false);
    EXPECT_FALSE(plug.contains("mirek"));
}

TEST(Home, FindBySpokenName) {
    const Home home = loaded_home();
    EXPECT_EQ(found(home, "living room"), "room Living room");
    EXPECT_EQ(found(home, "the livingroom"), "room Living room");
    EXPECT_EQ(found(home, "bedroom"), "room Bedroom");  // not "Bedroom lamp"
    EXPECT_EQ(found(home, "bedroom lamp"), "light Bedroom lamp");
    EXPECT_EQ(found(home, "desk"), "light Desk lamp");
    EXPECT_EQ(found(home, "reading"), "zone Reading");
    EXPECT_EQ(found(home, "all"), "home All lights");
    EXPECT_EQ(found(home, "the whole house"), "home All lights");
    EXPECT_EQ(found(home, "kitchen"), "-");
    EXPECT_THROW(home.find("lamp"), Ambiguous);  // Floor, Desk, Bedroom lamp
}

TEST(Home, FindById) {
    const Home home = loaded_home();
    EXPECT_EQ(home.find_id("living")->group->grouped_light, "gl-living");
    EXPECT_EQ(home.find_id("gl-bedroom")->group->name, "Bedroom");
    EXPECT_EQ(home.find_id("desk")->light->name, "Desk lamp");
    EXPECT_FALSE(home.find_id("nope"));
}

TEST(Home, Scenes) {
    const Home home = loaded_home();
    const Group* bedroom = home.find("bedroom")->group;
    EXPECT_EQ(home.scene("relax", bedroom)->id, "s-relax-bed");
    EXPECT_THROW(home.scene("relax", nullptr), Ambiguous);
    EXPECT_EQ(home.scene("movie", nullptr)->id, "s-movie");
    EXPECT_EQ(home.scene("movie", bedroom), nullptr);
    EXPECT_EQ(home.scene("night light", nullptr)->id, "s-night");
}

TEST(Home, ApplyUpdates) {
    Home home = loaded_home();
    const json events = json::parse(R"([
        {"type": "update", "id": "e1", "data": [
            {"id": "bedlamp", "type": "light", "on": {"on": true}, "dimming": {"brightness": 55.5}},
            {"id": "floor", "type": "light", "color_temperature": {"mirek": null, "mirek_valid": false}, "color": {"xy": {"x": 0.2, "y": 0.1}}},
            {"id": "gl-bedroom", "type": "grouped_light", "on": {"on": true}},
            {"id": "s-night", "type": "scene", "status": {"active": "static"}}
        ]}])");
    EXPECT_FALSE(home.apply(events));
    EXPECT_EQ(light_state(home, "bedlamp")["on"], true);
    EXPECT_EQ(light_state(home, "bedlamp")["brightness"], 56);
    EXPECT_FALSE(light_state(home, "floor").contains("mirek"));  // colour mode now
    EXPECT_EQ(light_state(home, "floor")["xy"][1], 0.1);
    EXPECT_TRUE(home.find("bedroom")->group->on);
}

TEST(Home, ChangesThatNeedAReload) {
    Home home = loaded_home();
    EXPECT_TRUE(home.apply(json::parse(R"([{"type": "add", "data": [{"id": "new", "type": "light"}]}])")));
    EXPECT_TRUE(home.apply(json::parse(R"([{"type": "delete", "data": [{"id": "s-movie", "type": "scene"}]}])")));
    EXPECT_TRUE(home.apply(json::parse(R"([{"type": "update", "data": [{"id": "living", "type": "room", "metadata": {"name": "Lounge"}}]}])")));
    EXPECT_TRUE(home.apply(json::parse(R"([{"type": "update", "data": [{"id": "unknown", "type": "light", "on": {"on": true}}]}])")));
    EXPECT_FALSE(home.apply(json::parse(R"([{"type": "add", "data": [{"id": "x", "type": "zigbee_connectivity"}]}])")));
    EXPECT_FALSE(home.apply(json::parse(R"({"not": "an array"})")));
}
