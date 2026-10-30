#include <gtest/gtest.h>
#include <map>

#include "fixture.hpp"
#include "lights.hpp"

using namespace buddy::hue;
using namespace buddy::hue::test;
using nlohmann::json;

namespace {

struct LightsTest : testing::Test {
    Home home = loaded_home();
    LightsConfig cfg{.default_room = "", .step = 20};

    Plan run(const std::string& action, const std::string& query = "", const std::string& device = "", const std::string& id = "") {
        return plan({.action = action, .query = query, .device = device, .id = id}, home, cfg);
    }
    // "<type>/<id> <body>" of the only request
    std::string sent(const Plan& p) {
        EXPECT_TRUE(p.ok) << p.say;
        EXPECT_EQ(p.requests.size(), 1u);
        if (p.requests.size() != 1) return "";
        return p.requests[0].type + "/" + p.requests[0].id + " " + p.requests[0].body.dump();
    }
};

} // namespace

TEST_F(LightsTest, OnOffToggle) {
    EXPECT_EQ(sent(run("on", "", "living room")), R"(grouped_light/gl-living {"on":{"on":true}})");
    EXPECT_EQ(sent(run("off", "", "desk lamp")), R"(light/desk {"on":{"on":false}})");
    EXPECT_EQ(sent(run("toggle", "", "bedroom")), R"(grouped_light/gl-bedroom {"on":{"on":true}})");  // is off
    EXPECT_EQ(sent(run("off", "", "", "floor")), R"(light/floor {"on":{"on":false}})");             // web: by id
}

TEST_F(LightsTest, DefaultTarget) {
    EXPECT_EQ(sent(run("off")), R"(grouped_light/gl-home {"on":{"on":false}})");  // no default room: every light
    cfg.default_room = "Living room";
    EXPECT_EQ(sent(run("off")), R"(grouped_light/gl-living {"on":{"on":false}})");
    EXPECT_EQ(sent(run("off", "", "everything")), R"(grouped_light/gl-home {"on":{"on":false}})");
    cfg.default_room = "Office";
    EXPECT_EQ(run("off").say, "I can't find the room Office.");
}

TEST_F(LightsTest, Brightness) {
    EXPECT_EQ(sent(run("brightness", "50", "bedroom")), R"(grouped_light/gl-bedroom {"dimming":{"brightness":50},"on":{"on":true}})");
    EXPECT_EQ(sent(run("brightness", "0", "bedroom")), R"(grouped_light/gl-bedroom {"on":{"on":false}})");
    EXPECT_EQ(run("brightness", "150", "bedroom").say, "Brightness goes from 0 to 100 percent.");
    EXPECT_EQ(run("brightness", "half", "bedroom").ok, false);
    EXPECT_EQ(sent(run("brighter", "", "desk")),
              R"(light/desk {"dimming_delta":{"action":"up","brightness_delta":20},"on":{"on":true}})");
    EXPECT_EQ(sent(run("darker", "", "desk")), R"(light/desk {"dimming_delta":{"action":"down","brightness_delta":20}})");
    EXPECT_EQ(run("darker", "", "fan plug").say, "The Fan plug can't dim.");
}

TEST_F(LightsTest, Colours) {
    EXPECT_EQ(sent(run("color", "blue", "living room")),
              R"(grouped_light/gl-living {"color":{"xy":{"x":0.153,"y":0.048}},"on":{"on":true}})");
    EXPECT_EQ(sent(run("color", "warm white", "bedroom lamp")),
              R"(light/bedlamp {"color_temperature":{"mirek":370},"on":{"on":true}})");
    EXPECT_EQ(sent(run("temperature", "4000", "bedroom")),
              R"(grouped_light/gl-bedroom {"color_temperature":{"mirek":250},"on":{"on":true}})");
    EXPECT_EQ(run("color", "blue", "bedroom lamp").say, "The Bedroom lamp can't change colour.");
    EXPECT_EQ(run("color", "blue", "bedroom").say, "The Bedroom lights can't change colour.");  // no colour light there
    EXPECT_EQ(run("color", "warm", "desk lamp").say, "The Desk lamp can't change its white.");
    EXPECT_EQ(run("color", "plaid", "living room").say, "I don't know the colour plaid.");
    EXPECT_EQ(run("temperature", "blue", "living room").ok, false);  // colours only via "color"
}

TEST_F(LightsTest, Scenes) {
    EXPECT_EQ(sent(run("scene", "movie night")), R"(scene/s-movie {"recall":{"action":"active"}})");
    EXPECT_EQ(sent(run("scene", "relax", "bedroom")), R"(scene/s-relax-bed {"recall":{"action":"active"}})");
    EXPECT_EQ(run("scene", "relax").say, "Which one, Relax in Bedroom or Relax in Living room?");
    cfg.default_room = "bedroom";
    EXPECT_EQ(sent(run("scene", "relax")), R"(scene/s-relax-bed {"recall":{"action":"active"}})");
    EXPECT_EQ(sent(run("scene", "movie")), R"(scene/s-movie {"recall":{"action":"active"}})");  // not in the default room: anywhere
    EXPECT_EQ(run("scene", "movie", "bedroom").say, "I don't know a scene called movie in Bedroom.");
    EXPECT_EQ(run("scene").say, "Which scene?");
}

TEST_F(LightsTest, Status) {
    EXPECT_EQ(run("status", "", "living room").say, "The Living room lights are on at 61 percent.");
    EXPECT_EQ(run("status", "", "bedroom lamp").say, "The Bedroom lamp is off.");
    EXPECT_EQ(run("status").say, "The lights are on at 61 percent.");
    EXPECT_TRUE(run("status").requests.empty());
}

TEST_F(LightsTest, Failures) {
    EXPECT_EQ(run("on", "", "kitchen").say, "I don't know a room or light called kitchen.");
    EXPECT_EQ(run("on", "", "lamp").say, "Which one, Bedroom lamp or Desk lamp?");
    EXPECT_EQ(run("on", "", "", "gone").say, "That light is gone.");
    EXPECT_EQ(plan({.action = "on"}, Home{}, cfg).say, "I can't reach the Hue bridge.");
    const Plan unknown = run("dance");
    EXPECT_FALSE(unknown.ok);
    EXPECT_TRUE(unknown.say.empty());
    EXPECT_TRUE(run("").ok);  // no command
}

TEST(Whites, Mirek) {
    EXPECT_EQ(white_mirek("2700"), 370);
    EXPECT_EQ(white_mirek("6500K"), 154);
    EXPECT_EQ(white_mirek("2000 kelvin"), 500);
    EXPECT_EQ(white_mirek("1500"), 500);  // clamped
    EXPECT_EQ(white_mirek("Daylight"), 153);
    EXPECT_EQ(white_mirek("50"), std::nullopt);
    EXPECT_EQ(white_mirek("blue"), std::nullopt);
}

TEST_F(LightsTest, SceneSaveCreates) {
    const Plan p = run("scene_save", " Reading time ", "", "living");
    ASSERT_TRUE(p.ok) << p.say;
    ASSERT_EQ(p.requests.size(), 1u);
    const Request& r = p.requests[0];
    EXPECT_EQ(r.method, "POST");
    EXPECT_EQ(r.type, "scene");
    EXPECT_EQ(r.body["metadata"], json({{"name", "Reading time"}}));
    EXPECT_EQ(r.body["group"], json({{"rid", "living"}, {"rtype", "room"}}));
    std::map<std::string, json> by_light;
    for (const auto& a : r.body["actions"]) by_light[a["target"]["rid"]] = a["action"];
    EXPECT_EQ(by_light.size(), 3u);
    // white mode: mirek wins over the approximate xy
    EXPECT_EQ(by_light["ceiling"], json::parse(R"({"on":{"on":true},"dimming":{"brightness":40.0},"color_temperature":{"mirek":366}})"));
    // dimmable white only: no colour fields (the bridge rejects them)
    EXPECT_EQ(by_light["desk"], json::parse(R"({"on":{"on":true},"dimming":{"brightness":100.0}})"));
}

TEST_F(LightsTest, SceneSaveOffLightsAndZones) {
    const Plan bed = run("scene_save", "Sleep", "bedroom");
    ASSERT_TRUE(bed.ok) << bed.say;
    for (const auto& a : bed.requests[0].body["actions"]) EXPECT_EQ(a["action"], json::parse(R"({"on":{"on":false}})"));
    const Plan zone = run("scene_save", "Focus", "reading");
    ASSERT_TRUE(zone.ok) << zone.say;
    EXPECT_EQ(zone.requests[0].body["group"]["rtype"], "zone");
}

TEST_F(LightsTest, SceneSaveOverwritesSameName) {
    const Plan p = run("scene_save", "movie NIGHT", "", "living");
    ASSERT_TRUE(p.ok) << p.say;
    ASSERT_EQ(p.requests.size(), 1u);
    EXPECT_EQ(p.requests[0].method, "PUT");
    EXPECT_EQ(p.requests[0].id, "s-movie");
    EXPECT_TRUE(p.requests[0].body.contains("actions"));
    EXPECT_FALSE(p.requests[0].body.contains("metadata"));  // keeps the bridge's name and picture
    // same name in another room: a new scene there
    EXPECT_EQ(run("scene_save", "Movie night", "", "bedroom").requests[0].method, "POST");
}

TEST_F(LightsTest, SceneSaveFails) {
    EXPECT_FALSE(run("scene_save", "  ", "", "living").ok);
    EXPECT_FALSE(run("scene_save", std::string(kSceneNameMax + 1, 'x'), "", "living").ok);
    EXPECT_TRUE(run("scene_save", std::string(kSceneNameMax, 'x'), "", "living").ok);
    EXPECT_EQ(run("scene_save", "Party", "all").say, "Scenes belong to a room or zone.");
    EXPECT_EQ(run("scene_save", "Party", "desk lamp").say, "Scenes belong to a room or zone.");
    EXPECT_TRUE(run("scene_save", "Party", "desk lamp").requests.empty());
}

TEST_F(LightsTest, SceneDelete) {
    Plan p = run("scene_delete", "", "", "s-night");  // web: by scene id
    ASSERT_TRUE(p.ok) << p.say;
    EXPECT_EQ(p.requests[0].method + " " + p.requests[0].type + "/" + p.requests[0].id, "DELETE scene/s-night");
    p = run("scene_delete", "relax", "bedroom");  // CLI: by name in a room
    ASSERT_TRUE(p.ok) << p.say;
    EXPECT_EQ(p.requests[0].id, "s-relax-bed");
    EXPECT_EQ(run("scene_delete", "", "", "gone").say, "That scene is gone.");
    EXPECT_FALSE(run("scene_delete", "relax", "all").ok);  // two rooms have one: ambiguous
}
