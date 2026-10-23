// spotify: voice commands → Spotify Web API, played by Spotify Soloist ("Buddy" Spotify Connect device) on the Pi.
//
//   buddy-spotify                      live: buddy/spotify/cmd in, buddy/spotify/state + buddy/voice/say out
//   buddy-spotify --login              once: OAuth login, saves the refresh token
//   buddy-spotify --cmd <action> [q]   test: runs one command and prints the result (no MQTT)
//
// Config (env, see deploy/buddy.env.example): BUDDY_SPOTIFY_*
#include <chrono>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "auth.hpp"
#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/queue.hpp"
#include "buddy/topics.hpp"
#include "http.hpp"
#include "player.hpp"

namespace {

using namespace buddy;
using namespace buddy::spotify;
using json = nlohmann::json;

struct Config {
    AuthConfig auth{
        .client_id = env_string("BUDDY_SPOTIFY_CLIENT_ID", ""),
        .redirect_uri = env_string("BUDDY_SPOTIFY_REDIRECT_URI", "http://127.0.0.1:8888/callback"),
        .token_file = env_string("BUDDY_SPOTIFY_TOKEN_FILE",
                                 env_string("HOME", ".") + "/buddy/config/spotify-token.json"),
    };
    PlayerConfig player{
        .device_name = env_string("BUDDY_SPOTIFY_DEVICE", "Buddy"),
        .volume_step = env_int("BUDDY_SPOTIFY_VOLUME_STEP", 10),
        .default_uri = env_string("BUDDY_SPOTIFY_DEFAULT_URI", ""),
    };
};

struct Command {
    std::string action;
    std::string query;
};

int serve(Player& player, Auth& auth) {
    if (!auth.logged_in())
        std::cout << "[spotify] not logged in yet: run ~/buddy/bin/buddy-spotify --login (commands will fail)"
                  << std::endl;

    BlockingQueue<Command> queue;  // HTTP runs on this thread, not on Paho's
    Mqtt mqtt("spotify", mqtt_config_from_env());
    mqtt.subscribe(topic::spotify_cmd, [&](const std::string&, const std::string& payload) {
        const auto msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
        if (msg.is_discarded() || !msg.contains("action") || !msg["action"].is_string()) {
            std::cout << "[spotify] bad command: " << payload << std::endl;
            return;
        }
        queue.push({msg["action"], msg.value("query", "")});
    });
    if (!mqtt.start()) return 0;

    json state = {{"playing", nullptr}, {"volume", nullptr}};
    while (running()) {
        const auto cmd = queue.pop(std::chrono::milliseconds(200));
        if (!cmd) continue;
        const auto t0 = std::chrono::steady_clock::now();
        const Result r = player.run(cmd->action, cmd->query);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
        std::cout << "[spotify] " << cmd->action << (cmd->query.empty() ? "" : " \"" + cmd->query + "\"")
                  << (r.ok ? " ok" : " failed") << " (" << ms.count() << " ms)" << std::endl;

        if (!r.say.empty()) mqtt.publish(topic::voice_say, json{{"text", r.say}}.dump(), 1);
        if (r.playing) state["playing"] = *r.playing;
        if (r.volume) state["volume"] = *r.volume;
        if (r.playing || r.volume) mqtt.publish(topic::spotify_state, state.dump(), 1, true);
    }

    mqtt.stop();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();

    const bool login = argc == 2 && std::strcmp(argv[1], "--login") == 0;
    const bool cmd = (argc == 3 || argc == 4) && std::strcmp(argv[1], "--cmd") == 0;
    if (argc != 1 && !login && !cmd) {
        std::cerr << "usage: " << argv[0] << " [--login | --cmd <action> [query]]" << std::endl;
        return 2;
    }

    std::optional<Config> cfg;
    try {
        cfg.emplace();
        if (cfg->auth.client_id.empty()) throw std::invalid_argument("BUDDY_SPOTIFY_CLIENT_ID is not set");
    } catch (const std::exception& e) {
        std::cerr << "[spotify] " << e.what() << std::endl;
        return 1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    Http http;
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
    } else if (cmd) {
        const Result r = player.run(argv[2], argc == 4 ? argv[3] : "");
        std::cout << (r.ok ? "ok" : "failed") << (r.say.empty() ? "" : ", says: \"" + r.say + "\"") << std::endl;
        rc = r.ok ? 0 : 1;
    } else {
        rc = serve(player, auth);
    }
    curl_global_cleanup();
    return rc;
}
