#pragma once
// Single source of truth for all MQTT topics.
//
// Scheme: buddy/<service>/<name>
//   status   – "online" / "offline", retained, "offline" is the last will (every service)
//   events   – something happened or a state changed; published only by the owning service
//   commands – something should happen; consumed only by the owning service
// Payloads are JSON objects (except status).

#include <string>
#include <string_view>

namespace buddy::topic {

// MQTT wildcard match: matches("buddy/+/status", "buddy/ears/status") == true
bool matches(std::string_view filter, std::string_view topic);

inline std::string status(std::string_view service) {
    return "buddy/" + std::string(service) + "/status";
}

// 1.5 skeleton test
inline constexpr std::string_view dev_ping = "buddy/dev/ping";                // event

// Phase 1 (names fixed now so later services agree)
inline constexpr std::string_view ears_wake       = "buddy/ears/wake";        // event
inline constexpr std::string_view ears_transcript = "buddy/ears/transcript";  // event
inline constexpr std::string_view brain_intent    = "buddy/brain/intent";     // event
inline constexpr std::string_view voice_say       = "buddy/voice/say";        // command
inline constexpr std::string_view voice_state     = "buddy/voice/state";      // event
inline constexpr std::string_view spotify_cmd     = "buddy/spotify/cmd";      // command
inline constexpr std::string_view spotify_state   = "buddy/spotify/state";    // event

// Phase 2 (planned). motion also listens to ears_wake / voice_state to animate eyes and head.
inline constexpr std::string_view motion_cmd      = "buddy/motion/cmd";       // command
inline constexpr std::string_view motion_state    = "buddy/motion/state";     // event

} // namespace buddy::topic
