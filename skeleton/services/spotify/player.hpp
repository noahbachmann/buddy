#pragma once
// Spotify Web API player calls on Spotify Connect devices. Which device:
//   - a device id in the command (web panel): exactly that device
//   - a speaker named in the command ("… on the bathroom speaker"): the best match in the device list
//   - play, play_playlist, play_liked without one: the music box (BUDDY_SPOTIFY_DEVICE, its name in the Spotify app)
//   - pause, next, volume, shuffle, …: the device playing right now, else the box
// Every command first fetches the device list (GET /me/player/devices), so it also works after a device got a new
// id. duck() / warm_up() fetch it ahead of time (on the wake word, while the user is still talking); the next
// command within 15 s uses it and saves a round trip.
//
// Nests (D20): Soloist (local_device, "Buddy") plays on the Pi and the speakers service sends its sound to a Nest over
// Bluetooth. The device list shows one entry per Nest (id "bt:<mac>", from buddy/speakers/state) in Soloist's place;
// commands to a Nest go to Soloist. Playing on one calls on_output first, so speakers connects it while Spotify
// starts; from one Nest to another while Soloist is active needs no Spotify call at all.
//
// Ducking: while Buddy listens or speaks, the active device plays at duck_percent of its volume if it is the box
// or in duck_devices (same room). Volume commands to it during a duck change the volume that restore() brings back,
// not the ducked one.

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "auth.hpp"
#include "buddy/http.hpp"

namespace buddy::spotify {

struct PlayerConfig {
    std::string device_name;   // BUDDY_SPOTIFY_DEVICE, the box's name in the Spotify app
    int volume_step = 10;      // BUDDY_SPOTIFY_VOLUME_STEP, percent
    std::string default_uri;   // BUDDY_SPOTIFY_DEFAULT_URI, played when there is nothing to resume
    int duck_percent = 30;     // BUDDY_SPOTIFY_DUCK_PERCENT, box volume while ducked, % of its volume; 100 = off
    std::string duck_file;     // remembers the volume to restore across a crash or restart (tmpfs); empty = off
    std::vector<std::string> duck_devices;  // BUDDY_SPOTIFY_DUCK_DEVICES, ducked like the box (same room)
    std::string local_device = "Buddy";     // BUDDY_SPOTIFY_LOCAL_DEVICE, Soloist's Connect name: hidden behind the Nests
};

// One skill command (brain: buddy/spotify/cmd).
struct Command {
    std::string action{};  // see Player::run; empty = no command (main loop: ears/voice state changed)
    std::string query{};
    std::string device{};  // speaker as spoken, empty = default for the action
    std::string device_id{};  // exact Connect device id (web panel); wins over `device`, no "on X" in the reply
    std::string fallback_action{};  // run instead (on the box) when `device` matches no speaker; empty = none
    std::string fallback_query{};
};

struct Result {
    bool ok = true;
    std::string say;                 // reply for voice; empty = nothing to say (the music is the answer)
    std::optional<bool> playing;
    std::optional<int> volume;
    std::optional<nlohmann::json> device;  // playback started or moved here: {id, name, type} for the panel at once
};

class Player {
public:
    Player(PlayerConfig cfg, Auth& auth, Http& http);

    // action: play, play_playlist, play_liked, pause, next, previous, volume_up, volume_down, set_volume, shuffle,
    // repeat, now_playing (see brain/intent.hpp for the queries), seek (query: position in ms; web panel only);
    // play without a query on a named speaker moves the music there. Never throws: errors become Result{ok = false, say = "…"} and a log line.
    Result run(const Command& c);

    // Refreshes the access token, opens the HTTPS connection and fetches the device list. Never throws; failures
    // are only logged (the command that follows reports them properly).
    void warm_up();

    // Turns the active device down if it plays and is in the room (box, duck_devices), and warms up like
    // warm_up(). Never throws. No-op when already ducked.
    void duck();
    // Brings back the volume from before duck(), plus volume commands since. Skipped if someone else (phone app)
    // changed the volume meanwhile. Never throws.
    void restore();
    // What Spotify plays right now on any device, as the state fields playing, volume, device, track, progress,
    // devices (shared/topics.json). Two requests. Never throws: nullopt on failure, logged once per failure streak.
    std::optional<nlohmann::json> snapshot();
    // Spotify Connect devices the account sees right now, one "name (type, active, volume %)" line each. Throws.
    std::vector<std::string> devices();

    // Call once at start: restores a duck that a crash or restart left behind (duck_file). Never throws.
    void recover();
    bool ducked() const { return duck_.has_value(); }
    // buddy/speakers/state: the Nests ({id, name}) and which one Soloist plays on. Until called, Soloist is listed
    // as itself (CLI runs without MQTT).
    void set_speakers(const nlohmann::json& state);
    // Called with a Nest's id (bt:<mac>) before playback starts on it: main sends buddy/speakers/cmd output.
    void on_output(std::function<void(const std::string&)> fn) { on_output_ = std::move(fn); }
    // Volume to report: while ducked, the one restore() brings back.
    std::optional<int> volume() const { return duck_ ? std::optional(duck_->restore_to) : std::nullopt; }

private:
    struct Device {
        std::string id;
        std::string name;
        bool active = false;
        int volume = 50;
        std::string type;
        std::string output;         // a Nest: its speakers id; `id` is then Soloist's
        bool local_active = false;  // a Nest: Soloist is the active device (on this Nest or another)
    };
    struct Duck {
        std::string device_id;
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

    Result dispatch(const Command& c);
    Result dispatch(const nlohmann::json& list, const Command& c);
    Result dispatch(const Device& d, const std::string& action, const std::string& query);  // Nests, Result::device
    Result dispatch_on(const Device& d, const std::string& action, const std::string& query);
    Result play(const Device& d, const std::string& query);
    Result play_playlist(const Device& d, const std::string& name);  // your own/followed; empty = default
    Result play_liked(const Device& d, bool shuffle);
    Result set_volume(const Device& d, const std::string& query);
    Result now_playing();
    const nlohmann::json& own_playlists();
    nlohmann::json best_own_playlist(const std::string& name, int min_score);
    HttpResponse api(const std::string& method, const std::string& path, const nlohmann::json& body = nullptr);
    void expect_ok(const HttpResponse& r, const std::string& what);
    static Device device_of(const nlohmann::json& d);
    nlohmann::json fetch_devices();  // the "devices" array, Soloist replaced by the Nests; remembers the box's id
    nlohmann::json with_nests(nlohmann::json list) const;
    Device box(const nlohmann::json& list) const;
    Device active(const nlohmann::json& list) const;  // the active device, else the box
    std::optional<Device> speaker(const nlohmann::json& list, const std::string& spoken) const;  // throws if ambiguous
    bool in_room(const std::string& name) const;
    void save_duck() const;

    PlayerConfig cfg_;
    Auth& auth_;
    Http& http_;
    std::optional<nlohmann::json> warm_list_;
    std::chrono::steady_clock::time_point warmed_at_{};
    std::string last_id_;          // last id the box was seen with, for when it drops out of the list in standby
    std::optional<Duck> duck_;
    nlohmann::json playlists_;  // own_playlists() cache
    std::chrono::steady_clock::time_point playlists_at_{};
    bool duck_disallowed_ = false;  // box refuses remote volume (403): stop trying
    bool snapshot_failing_ = false;  // log a failing snapshot once, not every poll
    std::optional<nlohmann::json> nests_;  // [{id, name}] from speakers; nullopt = never heard from it
    std::string selected_nest_;           // the Nest Soloist plays on
    std::function<void(const std::string&)> on_output_;
};

// Values a command just set, kept over polls that still show the old ones: Spotify reports a new volume 0.7–2.6 s
// later (measured 2026-10-29), and the panel's slider would jump back meanwhile.
class Expected {
public:
    using Clock = std::chrono::steady_clock;
    void set(const std::string& key, nlohmann::json value, Clock::time_point until) { fields_[key] = {std::move(value), until}; }
    // Puts the expected value into `fresh` where the poll still differs; forgets a key once the poll shows it or its
    // time is up (the device rounded it, or someone changed it again elsewhere).
    void apply(nlohmann::json& fresh, Clock::time_point now) {
        std::erase_if(fields_, [&](const auto& f) {
            const auto& [key, expected] = f;
            if (now >= expected.second || fresh.value(key, nlohmann::json()) == expected.first) return true;
            fresh[key] = expected.first;
            return false;
        });
    }

private:
    std::map<std::string, std::pair<nlohmann::json, Clock::time_point>> fields_;
};

// The progress to publish in `fresh` state: the published one (`old`) while it still extrapolates to within 2 s of
// the fresh poll (same track, same playing), else the fresh one. Keeps a playing song from re-publishing every poll;
// the browser moves the bar itself (ms + now - at).
nlohmann::json progress_to_publish(const nlohmann::json& old, const nlohmann::json& fresh);

} // namespace buddy::spotify
