// voice: text → speech (Piper) → Jabra speaker.
//
//   buddy-voice                              live: buddy/voice/say in, buddy/voice/state out
//   buddy-voice --say "text" [--wav f.wav]   offline test: speaks once (or writes a WAV), no MQTT
//
// Config (env, see deploy/buddy.env.example): BUDDY_VOICE_*, BUDDY_MODELS_DIR
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "audio_sink.hpp"
#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/queue.hpp"
#include "buddy/systemd.hpp"
#include "buddy/topics.hpp"
#include "tts.hpp"

namespace {

using namespace buddy;
using namespace buddy::voice;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

constexpr size_t kMaxChars = 400;  // a runaway message shouldn't talk for minutes
// A reply that waited this long behind others (or a voice that hung) is stale: skip it rather than answer late.
constexpr auto kMaxAge = std::chrono::seconds(10);

struct Say {
    std::string text;
    Clock::time_point received;
};

struct Config {
    std::string device = env_string("BUDDY_VOICE_DEVICE", "default");
    std::string models = env_string("BUDDY_MODELS_DIR", env_string("HOME", ".") + "/buddy/models");
    TtsConfig tts{
        .model_path = models + "/" + env_string("BUDDY_VOICE_MODEL", "en_GB-alan-medium.onnx"),
        .espeak_data_path = env_string("BUDDY_VOICE_ESPEAK_DATA", "/opt/libpiper/share/espeak-ng-data"),
        .length_scale = env_float("BUDDY_VOICE_LENGTH_SCALE", 1.0f),
        .gain = env_float("BUDDY_VOICE_GAIN", 1.0f),
        .threads = env_int("BUDDY_VOICE_THREADS", 3),
    };
};

long ms_since(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

// Speaks one text. Logs synthesis-to-first-audio and total time.
void say(Tts& tts, AudioSink& sink, std::string text) {
    if (text.size() > kMaxChars) text.resize(kMaxChars);
    const auto t0 = Clock::now();
    long first_ms = -1;
    size_t samples = 0;
    int rate = 0;
    bool open = false;
    try {
        tts.speak(text, [&](std::span<const int16_t> pcm, int sample_rate) {
            if (!open) {
                first_ms = ms_since(t0);
                rate = sample_rate;
                sink.begin(sample_rate);
                open = true;
            }
            sink.write(pcm);
            samples += pcm.size();
        });
        if (open) sink.end();
    } catch (const std::exception& e) {
        if (open) sink.end();
        std::cout << "[voice] \"" << text << "\" failed: " << e.what() << std::endl;
        return;
    }
    std::cout << "[voice] \"" << text << "\" (" << (rate ? samples * 1000 / rate : 0) << " ms audio, first audio after "
              << first_ms << " ms, done after " << ms_since(t0) << " ms)" << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();

    const char* text = nullptr;
    const char* wav = nullptr;
    if (argc >= 3 && std::strcmp(argv[1], "--say") == 0) {
        text = argv[2];
        if (argc == 5 && std::strcmp(argv[3], "--wav") == 0) wav = argv[4];
        else if (argc != 3) text = nullptr;
    }
    if (argc != 1 && !text) {
        std::cerr << "usage: " << argv[0] << " [--say \"text\" [--wav out.wav]]" << std::endl;
        return 2;
    }

    // Everything that can fail on bad config or a missing voice fails here, before touching MQTT.
    std::optional<Config> cfg;
    std::optional<Tts> tts;
    try {
        cfg.emplace();
        const auto t0 = Clock::now();
        tts.emplace(cfg->tts);
        std::cout << "[voice] loaded " << cfg->tts.model_path << " in " << ms_since(t0) << " ms" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[voice] " << e.what() << std::endl;
        return 1;
    }

    if (text) {
        std::unique_ptr<AudioSink> sink;
        if (wav) sink = std::make_unique<WavSink>(wav);
        else sink = std::make_unique<AlsaSink>(cfg->device);
        say(*tts, *sink, text);
        return 0;
    }

    AlsaSink sink(cfg->device);
    BlockingQueue<Say> queue;  // synthesis and playback run on this thread, not on Paho's
    on_stop([&] { queue.close(); });
    Mqtt mqtt("voice", mqtt_config_from_env());
    mqtt.subscribe(topic::voice_say, [&](const std::string&, const std::string& payload) {
        const auto msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
        if (msg.is_discarded() || !msg.contains("text") || !msg["text"].is_string()) {
            std::cout << "[voice] bad message: " << payload << std::endl;
            return;
        }
        queue.push({msg["text"], Clock::now()});
    });
    if (!mqtt.start()) return 0;

    auto set_state = [&](const char* s) { mqtt.publish(topic::voice_state, json{{"state", s}}.dump(), 1, true); };
    set_state("idle");
    while (running()) {
        systemd::notify_watchdog();
        auto msg = queue.pop(std::chrono::seconds(5));  // idle: watchdog only; a stop closes the queue
        if (!msg || msg->text.empty()) continue;
        if (Clock::now() - msg->received > kMaxAge) {
            std::cout << "[voice] skipped \"" << msg->text << "\": " << ms_since(msg->received) << " ms old" << std::endl;
            continue;
        }
        set_state("speaking");
        say(*tts, sink, std::move(msg->text));
        set_state("idle");
    }

    set_state("offline");
    mqtt.stop();
    return 0;
}
