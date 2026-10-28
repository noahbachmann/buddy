#pragma once
// Skill commands → CLIP v2 PUTs, without I/O: plan() resolves the target and builds the requests; main.cpp sends
// them through a Throttle. Failures and answers come back as text for buddy/voice/say.
//
// Actions (buddy/hue/cmd {action, [query], [device], [id]}):
//   on, off, toggle
//   brightness   query 0..100 (0 = off)
//   brighter, darker               by BUDDY_HUE_STEP percent
//   color        query: colour name ("blue"), or a white ("warm white", "daylight": like temperature)
//   temperature  query: kelvin ("2700") or a white
//   scene        query: scene name, in the target room if one is named
//   status       says whether the target is on, and how bright
// Target: id (web panel: room/zone/light id) > device (spoken name) > BUDDY_HUE_DEFAULT_ROOM > every light.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bridge.hpp"
#include "home.hpp"

namespace buddy::hue {

struct LightsConfig {
    std::string default_room;  // BUDDY_HUE_DEFAULT_ROOM: the room Buddy stands in; empty = every light
    int step = 20;             // BUDDY_HUE_STEP, percent for brighter/darker
};

struct Command {
    std::string action{};  // empty = no command
    std::string query{};
    std::string device{};
    std::string id{};
};

struct Plan {
    bool ok = true;
    std::string say;  // spoken reply, empty = none (the lights changing is the answer)
    std::vector<Request> requests;
};

Plan plan(const Command& c, const Home& home, const LightsConfig& cfg);

// Colour words → CIE xy (the bridge maps it into each light's gamut). nullopt = not a colour we know.
std::optional<std::pair<double, double>> color_xy(const std::string& name);
// White words ("warm white", "cool", "daylight") or kelvin ("2700", "2700k") → mirek, clamped to 153..500.
std::optional<int> white_mirek(const std::string& name);

} // namespace buddy::hue
