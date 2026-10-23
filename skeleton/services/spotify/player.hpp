#pragma once
// Spotify Web API player calls for one Spotify Connect device: Spotify Soloist on the Pi, named "Buddy".
// Every command first looks the device up (GET /me/player/devices), so it also works right after
// Soloist restarted with a new device id. warm_up() does that lookup ahead of time (on the wake word, while
// the user is still talking); the next command within 15 s uses it and saves a round trip.

#include <chrono>
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

    // action: play, play_playlist, pause, next, previous, volume_up, volume_down. Never throws: errors become
    // Result{ok = false, say = "…"} and a log line.
    Result run(const std::string& action, const std::string& query);

    // Refreshes the access token, opens the HTTPS connection and looks the device up. Never throws; failures
    // are only logged (the command that follows reports them properly).
    void warm_up();

private:
    struct Device {
        std::string id;
        bool active = false;
        int volume = 50;
    };
    // Error with a short sentence for voice; what() is the detail for the log.
    struct Failure : std::runtime_error {
        Failure(const std::string& detail, std::string say, long status = 0)
            : std::runtime_error(detail), say(std::move(say)), status(status) {}
        std::string say;
        long status;  // HTTP status, 0 = not an HTTP error
    };

    Result dispatch(const std::string& action, const std::string& query);
    Result dispatch(const Device& d, const std::string& action, const std::string& query);
    Result play(const Device& d, const std::string& query);
    Result play_playlist(const Device& d, const std::string& name);  // your own/followed; empty = default
    HttpResponse api(const std::string& method, const std::string& path, const nlohmann::json& body = nullptr);
    void expect_ok(const HttpResponse& r, const std::string& what);
    Device device();

    PlayerConfig cfg_;
    Auth& auth_;
    Http& http_;
    std::optional<Device> warm_device_;
    std::chrono::steady_clock::time_point warmed_at_{};
};

} // namespace buddy::spotify
