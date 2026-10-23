#pragma once
// Spotify Web API player calls for one Spotify Connect device: Spotify Soloist on the Pi, named "Buddy".
// Every command first looks the device up (GET /me/player/devices), so it also works right after
// Soloist restarted with a new device id.

#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "auth.hpp"
#include "http.hpp"

namespace buddy::spotify {

struct PlayerConfig {
    std::string device_name = "Buddy";  // BUDDY_SPOTIFY_DEVICE = soloist --device-name
    int volume_step = 10;               // BUDDY_SPOTIFY_VOLUME_STEP, percent
    std::string default_uri;            // BUDDY_SPOTIFY_DEFAULT_URI, played when there is nothing to resume
};

struct Result {
    bool ok = true;
    std::string say;                 // reply for voice; empty = nothing to say (the music is the answer)
    std::optional<bool> playing;
    std::optional<int> volume;
};

class Player {
public:
    Player(PlayerConfig cfg, Auth& auth, Http& http);

    // action: play, pause, next, previous, volume_up, volume_down. Never throws: errors become
    // Result{ok = false, say = "…"} and a log line.
    Result run(const std::string& action, const std::string& query);

private:
    struct Device {
        std::string id;
        bool active = false;
        int volume = 50;
    };
    // Error with a short sentence for voice; what() is the detail for the log.
    struct Failure : std::runtime_error {
        Failure(const std::string& detail, std::string say) : std::runtime_error(detail), say(std::move(say)) {}
        std::string say;
    };

    Result dispatch(const std::string& action, const std::string& query);
    Result play(const Device& d, const std::string& query);
    HttpResponse api(const std::string& method, const std::string& path, const nlohmann::json& body = nullptr);
    void expect_ok(const HttpResponse& r, const std::string& what);
    Device device();

    PlayerConfig cfg_;
    Auth& auth_;
    Http& http_;
};

} // namespace buddy::spotify
