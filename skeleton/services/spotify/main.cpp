// spotify: voice commands → Spotify Web API, played on the music box (a Spotify Connect device, BUDDY_SPOTIFY_DEVICE)
// or any other Connect speaker the command names. Also ducks the box while Buddy listens or speaks.
//
//   buddy-spotify                      live: buddy/spotify/cmd in, buddy/spotify/state + buddy/voice/say out;
//                                      buddy/ears/state + buddy/voice/state duck the box (and warm up token,
//                                      connection and device for the command that follows); while a web panel
//                                      watches ({"action": "watch"} every 20 s), polls what plays on any device
//   buddy-spotify --login              once: OAuth login, saves the refresh token
//   buddy-spotify --cmd <action> [q] [speaker]
//                                      test: runs one command and prints the result (no MQTT)
//   buddy-spotify --cmd devices        test: lists the Connect devices Spotify sees (exact names for BUDDY_SPOTIFY_DEVICE)
//   buddy-spotify --cmd duck|restore   test: turns the box down / back up (the volume to restore is kept in
//                                      $XDG_RUNTIME_DIR between the two calls)
//
// Config (env, see deploy/buddy.env.example): BUDDY_SPOTIFY_*
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "auth.hpp"
#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/queue.hpp"
#include "buddy/systemd.hpp"
#include "buddy/topics.hpp"
#include "buddy/curl_http.hpp"
#include "player.hpp"

namespace {

using namespace buddy;
using namespace buddy::spotify;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// "desktop-1, Bathroom speaker" → {"desktop-1", "Bathroom speaker"}
std::vector<std::string> name_list(const std::string& csv) {
    std::vector<std::string> out;
    std::istringstream in(csv);
    for (std::string name; std::getline(in, name, ',');) {
        name.erase(0, name.find_first_not_of(' '));
        name.erase(name.find_last_not_of(' ') + 1);
        if (!name.empty()) out.push_back(name);
    }
    return out;
}

struct Config {
    AuthConfig auth{
        .client_id = env_string("BUDDY_SPOTIFY_CLIENT_ID", ""),
        .redirect_uri = env_string("BUDDY_SPOTIFY_REDIRECT_URI", "http://127.0.0.1:8888/callback"),
        .token_file = env_string("BUDDY_SPOTIFY_TOKEN_FILE",
                                 env_string("HOME", ".") + "/buddy/config/spotify-token.json"),
    };
    PlayerConfig player{
        .device_name = env_string("BUDDY_SPOTIFY_DEVICE", ""),
        .volume_step = env_int("BUDDY_SPOTIFY_VOLUME_STEP", 10),
        .default_uri = env_string("BUDDY_SPOTIFY_DEFAULT_URI", ""),
        .duck_percent = env_int("BUDDY_SPOTIFY_DUCK_PERCENT", 30),
        .duck_file = std::getenv("XDG_RUNTIME_DIR") ? std::string(std::getenv("XDG_RUNTIME_DIR")) + "/buddy-spotify-duck.json"
                                                    : "",
        .duck_devices = name_list(env_string("BUDDY_SPOTIFY_DUCK_DEVICES", "")),
        .local_device = env_string("BUDDY_SPOTIFY_LOCAL_DEVICE", "Buddy"),
    };
    int duck_hold_ms = env_int("BUDDY_SPOTIFY_DUCK_HOLD_MS", 1500);
    int poll_s = env_int("BUDDY_SPOTIFY_POLL_S", 5);
};

int serve(Player& player, Auth& auth, const Config& cfg) {
    if (!auth.logged_in())
        std::cout << "[spotify] not logged in yet: run ~/buddy/bin/buddy-spotify --login (commands will fail)"
                  << std::endl;

    BlockingQueue<Command> queue;  // HTTP runs on this thread, not on Paho's
    on_stop([&] { queue.close(); });
    std::atomic<bool> listening{false}, speaking{false}, watch{false};
    auto state_of = [](const std::string& payload) {
        const auto msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
        return msg.is_object() ? msg.value("state", "") : std::string();
    };
    Mqtt mqtt("spotify", mqtt_config_from_env());
    mqtt.subscribe(topic::spotify_cmd, [&](const std::string&, const std::string& payload) {
        const auto msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
        if (msg.is_discarded() || !msg.contains("action") || !msg["action"].is_string() || msg["action"] == "") {
            std::cout << "[spotify] bad command: " << payload << std::endl;
            return;
        }
        if (msg["action"] == "watch") {  // panel heartbeat: frequent, never logged
            watch = true;
            queue.push({});
            return;
        }
        auto text = [&](const char* key) { return msg.contains(key) && msg[key].is_string() ? msg[key].get<std::string>() : ""; };
        Command c{.action = msg["action"], .query = text("query"), .device = text("device"), .device_id = text("id")};
        if (const auto f = msg.value("fallback", json()); f.is_object() && f["action"].is_string()) {
            c.fallback_action = f["action"];
            c.fallback_query = f.value("query", "");
        }
        queue.push(std::move(c));
    });
    mqtt.subscribe(topic::ears_state, [&](const std::string&, const std::string& payload) {
        const auto s = state_of(payload);
        listening = s == "listening" || s == "transcribing";
        queue.push({});
    });
    std::mutex nests_mutex;  // the Nests (D20), handed to the player on its thread
    std::optional<json> nests;
    mqtt.subscribe(topic::speakers_state, [&](const std::string&, const std::string& payload) {
        auto msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
        if (!msg.is_object()) return;
        std::lock_guard lock(nests_mutex);
        nests = std::move(msg);
        queue.push({});
    });
    mqtt.subscribe(topic::voice_state, [&](const std::string&, const std::string& payload) {
        speaking = state_of(payload) == "speaking";
        queue.push({});
    });
    player.recover();  // a crash while ducked must not leave the box quiet
    if (!mqtt.start()) return 0;

    json state = {{"playing", nullptr}, {"volume", nullptr}, {"ducked", false}, {"device", nullptr}, {"track", nullptr},
                  {"progress", nullptr}, {"devices", json::array()}};
    json published;
    auto publish_state = [&] {
        if (state == published) return;
        mqtt.publish(topic::spotify_state, state.dump(), 1, true);
        published = state;
    };
    publish_state();

    // Spotify has no push: poll while a panel is open (watched_until), else stay quiet.
    const auto poll_every = std::chrono::seconds(std::max(cfg.poll_s, 1));
    auto watched_until = Clock::now(), next_poll = Clock::now();
    Expected expected;

    // Duck on the first sign of activity (the wake word); restore after hold_ms of quiet, which covers the gap
    // between the transcript and the reply (brain → spotify → voice) so the music doesn't jump up and down.
    const auto hold = std::chrono::milliseconds(cfg.duck_hold_ms);
    bool was_busy = false;
    auto quiet_since = Clock::now();
    while (running()) {
        systemd::notify_watchdog();
        std::chrono::milliseconds timeout = std::chrono::seconds(5);  // idle: watchdog only; a stop closes the queue
        if (player.ducked() && !was_busy)
            timeout = std::clamp(std::chrono::duration_cast<std::chrono::milliseconds>(quiet_since + hold - Clock::now()),
                                 std::chrono::milliseconds(0), timeout);
        if (Clock::now() < watched_until)
            timeout = std::clamp(std::chrono::duration_cast<std::chrono::milliseconds>(next_poll - Clock::now()),
                                 std::chrono::milliseconds(0), timeout);
        const auto cmd = queue.pop(timeout);
        if (std::lock_guard lock(nests_mutex); nests) {
            player.set_speakers(*nests);
            nests.reset();
            if (Clock::now() < watched_until) next_poll = Clock::now();  // another Nest active: show it
        }

        if (watch.exchange(false)) {
            if (Clock::now() >= watched_until) next_poll = Clock::now();  // panel just opened: now, not in 5 s
            watched_until = Clock::now() + std::chrono::seconds(30);
        }
        if (Clock::now() < watched_until && Clock::now() >= next_poll) {
            if (auto fresh = player.snapshot()) {
                expected.apply(*fresh, Clock::now());
                (*fresh)["progress"] = progress_to_publish(state, *fresh);
                for (const auto& [key, value] : fresh->items()) state[key] = value;
            }
            next_poll = Clock::now() + poll_every;
        }

        const bool busy = listening || speaking;
        if (busy && !was_busy) player.duck();  // most commands follow: also warms up token, connection, device
        if (!busy && was_busy) quiet_since = Clock::now();
        was_busy = busy;
        if (!busy && player.ducked() && Clock::now() - quiet_since >= hold) {
            player.restore();
            if (player.ducked()) quiet_since = Clock::now();  // failed: retry after another hold
        }
        state["ducked"] = player.ducked();
        if (!cmd || cmd->action.empty()) {
            publish_state();
            continue;
        }

        const auto t0 = Clock::now();
        const Result r = player.run(*cmd);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0);
        std::cout << "[spotify] " << cmd->action << (cmd->query.empty() ? "" : " \"" + cmd->query + "\"")
                  << (cmd->device.empty() ? "" : " on \"" + cmd->device + "\"")
                  << (cmd->device_id.empty() ? "" : " on " + cmd->device_id) << (r.ok ? " ok" : " failed") << " (" << ms.count() << " ms)" << std::endl;

        if (!r.output.empty()) mqtt.publish(topic::speakers_cmd, json{{"action", "output"}, {"speaker", r.output}}.dump(), 1);
        if (!r.say.empty()) mqtt.publish(topic::voice_say, json{{"text", r.say}}.dump(), 1);
        const auto until = Clock::now() + std::chrono::seconds(8);
        if (r.playing) expected.set("playing", state["playing"] = *r.playing, until);
        if (r.volume) expected.set("volume", state["volume"] = *r.volume, until);
        publish_state();
        // Spotify shows a change about a second later
        if (Clock::now() < watched_until) next_poll = std::min(next_poll, Clock::now() + std::chrono::milliseconds(1500));
    }

    player.restore();
    state["ducked"] = player.ducked();
    publish_state();
    mqtt.stop();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();

    const bool login = argc == 2 && std::strcmp(argv[1], "--login") == 0;
    const bool cmd = argc >= 3 && argc <= 5 && std::strcmp(argv[1], "--cmd") == 0;
    if (argc != 1 && !login && !cmd) {
        std::cerr << "usage: " << argv[0] << " [--login | --cmd <action> [query] [speaker]]" << std::endl;
        return 2;
    }

    std::optional<Config> cfg;
    try {
        cfg.emplace();
        if (cfg->auth.client_id.empty()) throw std::invalid_argument("BUDDY_SPOTIFY_CLIENT_ID is not set");
        if (!login && cfg->player.device_name.empty())
            throw std::invalid_argument("BUDDY_SPOTIFY_DEVICE is not set (the music box's name in the Spotify app)");
        // Spotify client IDs are 32 hex digits. Anything else is usually the client secret pasted by mistake;
        // Spotify then answers "INVALID_CLIENT" at login.
        const auto& id = cfg->auth.client_id;
        if (id.size() != 32 || id.find_first_not_of("0123456789abcdef") != std::string::npos)
            std::cerr << "[spotify] warning: BUDDY_SPOTIFY_CLIENT_ID has " << id.size()
                      << " characters, expected 32 hex digits (dashboard → your app → Settings → Client ID)"
                      << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[spotify] " << e.what() << std::endl;
        return 1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CurlHttp http;
    Auth auth(cfg->auth, http);
    Player player(cfg->player, auth, http);

    int rc = 0;
    if (login) {
        try {
            auth.login(std::cin, std::cout);
        } catch (const std::exception& e) {
            std::cerr << "✗ " << e.what() << std::endl;
            rc = 1;
        }
    } else if (cmd && std::strcmp(argv[2], "devices") == 0) {
        try {
            const auto list = player.devices();
            for (const auto& d : list) std::cout << d << std::endl;
            if (list.empty()) std::cout << "no devices: open Spotify on the box or a phone first" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "✗ " << e.what() << std::endl;
            rc = 1;
        }
    } else if (cmd && std::strcmp(argv[2], "duck") == 0) {
        player.duck();
        std::cout << (player.ducked() ? "ducked" : "not ducked (box not active or not playing?)") << std::endl;
    } else if (cmd && std::strcmp(argv[2], "restore") == 0) {
        player.recover();
    } else if (cmd) {
        const Result r = player.run({.action = argv[2], .query = argc >= 4 ? argv[3] : "", .device = argc == 5 ? argv[4] : ""});
        std::cout << (r.ok ? "ok" : "failed") << (r.say.empty() ? "" : ", says: \"" + r.say + "\"") << std::endl;
        rc = r.ok ? 0 : 1;
    } else {
        rc = serve(player, auth, *cfg);
    }
    curl_global_cleanup();
    return rc;
}
