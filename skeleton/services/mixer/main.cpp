// mixer: turns the music down while Buddy listens or speaks (ducking).
//
//   buddy-mixer                      live: buddy/ears/state + buddy/voice/state in, buddy/mixer/state out
//   buddy-mixer --duck | --restore   test: sets the music volume once (no MQTT)
//
// "Music" = everything playing into the PipeWire sink BUDDY_MIXER_SINK (buddy_music, deploy/pipewire/buddy-audio.conf).
// Its volume is set with pactl (pipewire-pulse), so Spotify's own volume is never touched.
// Runs on the Pi: it controls the local PipeWire session.
//
// Config (env, see deploy/buddy.env.example): BUDDY_MIXER_*
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>

#include <spawn.h>
#include <sys/wait.h>

#include <nlohmann/json.hpp>

#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/systemd.hpp"
#include "buddy/topics.hpp"

extern char** environ;

namespace {

using namespace buddy;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct Config {
    std::string sink = env_string("BUDDY_MIXER_SINK", "buddy_music");
    float duck_db = env_float("BUDDY_MIXER_DUCK_DB", -20.0f);
    int hold_ms = env_int("BUDDY_MIXER_HOLD_MS", 1500);
};

// Sets the sink volume as a linear factor (1.0 = unchanged). pactl's "0.1" is absolute and linear; a leading
// sign would mean relative, so the value is always positive. Errors go to stderr (journald) from pactl itself.
bool set_volume(const std::string& sink, float factor) {
    char value[32];
    std::snprintf(value, sizeof value, "%.4f", factor);
    const char* argv[] = {"pactl", "set-sink-volume", sink.c_str(), value, nullptr};
    pid_t pid;
    if (const int err = posix_spawnp(&pid, "pactl", nullptr, nullptr, const_cast<char**>(argv), environ); err != 0) {
        std::cout << "[mixer] cannot run pactl: " << std::strerror(err) << " (apt install pulseaudio-utils)"
                  << std::endl;
        return false;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

float db_to_factor(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();

    const bool duck = argc == 2 && std::strcmp(argv[1], "--duck") == 0;
    const bool restore = argc == 2 && std::strcmp(argv[1], "--restore") == 0;
    if (argc != 1 && !duck && !restore) {
        std::cerr << "usage: " << argv[0] << " [--duck | --restore]" << std::endl;
        return 2;
    }

    std::optional<Config> cfg;
    try {
        cfg.emplace();
    } catch (const std::exception& e) {
        std::cerr << "[mixer] " << e.what() << std::endl;
        return 1;
    }
    const float ducked_factor = db_to_factor(cfg->duck_db);

    if (duck || restore) {
        const bool ok = set_volume(cfg->sink, duck ? ducked_factor : 1.0f);
        std::cout << (ok ? "ok" : "failed (is the sink there? pactl list short sinks)") << std::endl;
        return ok ? 0 : 1;
    }

    // Set from Paho's thread, read by the loop below; the loop sleeps until one of them changes.
    std::mutex mutex;
    std::condition_variable changed;
    bool listening = false, speaking = false;
    auto set = [&](bool& flag, bool value) {
        {
            std::lock_guard lock(mutex);
            flag = value;
        }
        changed.notify_one();
    };
    auto state_of = [](const std::string& payload) {
        const auto msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
        return msg.is_object() ? msg.value("state", "") : std::string();
    };

    Mqtt mqtt("mixer", mqtt_config_from_env());
    mqtt.subscribe(topic::ears_state, [&](const std::string&, const std::string& payload) {
        const auto s = state_of(payload);
        set(listening, s == "listening" || s == "transcribing");
    });
    mqtt.subscribe(topic::voice_state, [&](const std::string&, const std::string& payload) {
        set(speaking, state_of(payload) == "speaking");
    });

    // A crash or power cut may have left the music ducked.
    set_volume(cfg->sink, 1.0f);
    if (!mqtt.start()) return 0;

    auto publish = [&](bool ducked) { mqtt.publish(topic::mixer_state, json{{"ducked", ducked}}.dump(), 1, true); };
    publish(false);
    std::cout << "[mixer] ducking \"" << cfg->sink << "\" by " << cfg->duck_db << " dB, hold " << cfg->hold_ms << " ms"
              << std::endl;

    // Restore only after hold_ms of quiet: between the end of the transcript and the reply (brain → skill →
    // voice) nothing is "busy", and the music shouldn't jump up and down again.
    bool ducked = false, was_busy = false;
    auto quiet_since = Clock::now();
    while (running()) {
        systemd::notify_watchdog();
        bool busy;
        {
            // Wake on a state change; otherwise at the end of the hold time, or after 200 ms to re-check running().
            std::unique_lock lock(mutex);
            auto deadline = Clock::now() + std::chrono::milliseconds(200);
            if (ducked && !listening && !speaking)
                deadline = std::min(deadline, quiet_since + std::chrono::milliseconds(cfg->hold_ms));
            changed.wait_until(lock, deadline);
            busy = listening || speaking;
        }
        if (!busy && was_busy) quiet_since = Clock::now();  // hold counts from the end of activity
        was_busy = busy;
        if (busy) {
            if (!ducked) {
                if (!set_volume(cfg->sink, ducked_factor)) std::cout << "[mixer] duck failed" << std::endl;
                ducked = true;  // even on failure: don't retry on every wake-up
                publish(true);
            }
        } else if (ducked && Clock::now() - quiet_since >= std::chrono::milliseconds(cfg->hold_ms)) {
            if (!set_volume(cfg->sink, 1.0f)) std::cout << "[mixer] restore failed" << std::endl;
            ducked = false;
            publish(false);
        }
    }

    set_volume(cfg->sink, 1.0f);
    publish(false);
    mqtt.stop();
    return 0;
}
