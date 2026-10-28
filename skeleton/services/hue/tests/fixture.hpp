#pragma once
// tests/resources.json: a bridge in CLIP v2 shape. Rooms "Living room" (Ceiling, Floor lamp: colour; Desk lamp:
// dimmable white) and "Bedroom" (Bedroom lamp: white ambiance; Fan plug: on/off), zone "Reading" (Floor lamp,
// Desk lamp), room "Garage" without lights, scenes "Relax" in both rooms, "Movie night", "Nightlight".

#include <fstream>

#include <nlohmann/json.hpp>

#include "home.hpp"

namespace buddy::hue::test {

inline nlohmann::json resources() {
    std::ifstream in(BUDDY_SOURCE_DIR "/services/hue/tests/resources.json");
    return nlohmann::json::parse(in)["data"];
}

inline Home loaded_home() {
    Home home;
    home.load(resources());
    return home;
}

} // namespace buddy::hue::test
