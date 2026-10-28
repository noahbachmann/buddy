#pragma once
// Text → intent. The rule parser is the first implementation; an LLM-backed parser can replace it later
// without touching the routing in main.cpp.

#include <optional>
#include <string>
#include <string_view>

namespace buddy::brain {

struct Intent {
    std::string name;   // spotify: play, play_playlist, play_liked, pause, next, previous, volume_up, volume_down,
                        // set_volume, shuffle, repeat, now_playing
                        // hue: on, off, brightness, brighter, darker, color, scene, status
    std::string skill;  // service that handles it: "spotify", "hue"
    std::string query;  // optional argument: "daft punk" (play), "chill vibes" (play_playlist), "50" (set_volume,
                        // brightness), "on"/"off" (shuffle), "track"/"context"/"off" (repeat), "shuffle" (play_liked),
                        // "blue"/"warm white" (color), "movie" (scene)
    std::string device;  // speaker ("… on the bathroom speaker") or room/light ("bedroom lights off"), as spoken;
                         // empty = the skill's default
    // What the text means if `device` names no real speaker ("play shine on you crazy diamond"); the skill knows
    // its speakers, brain doesn't. Empty = no other reading.
    std::string fallback_name;
    std::string fallback_query;
};

class IntentParser {
public:
    virtual ~IntentParser() = default;
    // nullopt = not understood
    virtual std::optional<Intent> parse(std::string_view text) const = 0;
};

} // namespace buddy::brain
