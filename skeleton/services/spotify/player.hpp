#pragma once
// Spotify Web API player calls for one Spotify Connect device: the music box (BUDDY_SPOTIFY_DEVICE, its name in
// the Spotify app). Every command first looks the device up (GET /me/player/devices), so it also works after the
// box got a new device id. duck() / warm_up() do that lookup ahead of time (on the wake word, while the user is
// still talking); the next command within 15 s uses it and saves a round trip.
//
// Ducking: while Buddy listens or speaks, the box plays at duck_percent of its volume. Volume commands during a
// duck change the volume that restore() brings back, not the ducked one.

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "auth.hpp"
#include "http.hpp"

namespace buddy::spotify {

struct PlayerConfig {
    std::string device_name;   // BUDDY_SPOTIFY_DEVICE, the box's name in the Spotify app
    int volume_step = 10;      // BUDDY_SPOTIFY_VOLUME_STEP, percent
    std::string default_uri;   // BUDDY_SPOTIFY_DEFAULT_URI, played when there is nothing to resume
    int duck_percent = 30;     // BUDDY_SPOTIFY_DUCK_PERCENT, box volume while ducked, % of its volume; 100 = off
    std::string duck_file;     // remembers the volume to restore across a crash or restart (tmpfs); empty = off
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

    // action: play, play_playlist, play_liked, pause, next, previous, volume_up, volume_down, set_volume, shuffle,
    // repeat, now_playing (see brain/intent.hpp for the queries). Never throws: errors become
    // Result{ok = false, say = "…"} and a log line.
    Result run(const std::string& action, const std::string& query);

    // Refreshes the access token, opens the HTTPS connection and looks the device up. Never throws; failures
    // are only logged (the command that follows reports them properly).
    void warm_up();

    // Turns the box down if it is playing, and warms up like warm_up(). Never throws. No-op when already ducked.
    void duck();
    // Brings back the volume from before duck(), plus volume commands since. Skipped if someone else (phone app)
    // changed the volume meanwhile. Never throws.
    void restore();
    // Spotify Connect devices the account sees right now, one "name (active, volume %)" line each. Throws.
    std::vector<std::string> devices();

    // Call once at start: restores a duck that a crash or restart left behind (duck_file). Never throws.
    void recover();
    bool ducked() const { return duck_.has_value(); }
    // Volume to report: while ducked, the one restore() brings back.
    std::optional<int> volume() const { return duck_ ? std::optional(duck_->restore_to) : std::nullopt; }

private:
    struct Device {
        std::string id;
        bool active = false;
        int volume = 50;
    };
    struct Duck {
        int restore_to;  // volume before ducking, changed by volume commands while ducked
        int ducked_to;   // what duck() set; restore() only acts if the box still has it
        int attempts = 0;  // failed restores (network, box gone); given up after a few
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
    Result play_liked(const Device& d, bool shuffle);
    Result set_volume(const std::string& query);
    Result now_playing();
    const nlohmann::json& own_playlists();
    nlohmann::json best_own_playlist(const std::string& name, int min_score);
    HttpResponse api(const std::string& method, const std::string& path, const nlohmann::json& body = nullptr);
    void expect_ok(const HttpResponse& r, const std::string& what);
    Device device();
    void save_duck() const;

    PlayerConfig cfg_;
    Auth& auth_;
    Http& http_;
    std::optional<Device> warm_device_;
    std::chrono::steady_clock::time_point warmed_at_{};
    std::string last_id_;          // last id the box was seen with, for when it drops out of the list in standby
    std::optional<Duck> duck_;
    nlohmann::json playlists_;  // own_playlists() cache
    std::chrono::steady_clock::time_point playlists_at_{};
    bool duck_disallowed_ = false;  // box refuses remote volume (403): stop trying
};

} // namespace buddy::spotify
