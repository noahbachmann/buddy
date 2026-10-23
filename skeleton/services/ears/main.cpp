// ears: microphone → wake word (openWakeWord) → end of speech (Silero VAD) → text (whisper.cpp) → MQTT.
//
//   buddy-ears              live: ALSA "default" device, publishes on MQTT
//   buddy-ears --wav f.wav  offline test: reads a 16 kHz mono WAV, prints instead of publishing (no MQTT)
//   buddy-ears --transcribe f.wav…  speech-to-text only, one line per file: "<file>\t<ms>\t<text>" (compare models)
//
// Config (env, see deploy/buddy.env.example): BUDDY_EARS_*, BUDDY_WAKE_*, BUDDY_VAD_*, BUDDY_STT_*, BUDDY_MODELS_DIR
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>

#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

#include "audio_source.hpp"
#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/topics.hpp"
#include "stt.hpp"
#include "vad.hpp"
#include "wake_word.hpp"

namespace {

using namespace buddy;
using namespace buddy::ears;
using Clock = std::chrono::steady_clock;
using Publish = std::function<void(std::string_view topic, const nlohmann::json& msg, bool retained)>;

struct Config {
    std::string device = env_string("BUDDY_EARS_DEVICE", "default");
    bool debug = env_int("BUDDY_EARS_DEBUG", 0) != 0;
    // Buddy hearing itself: even with echo cancellation, a reply could contain something wake-word-like.
    bool ignore_while_speaking = env_int("BUDDY_EARS_IGNORE_WHILE_SPEAKING", 1) != 0;
    std::string models = env_string("BUDDY_MODELS_DIR", env_string("HOME", ".") + "/buddy/models");
    WakeWordConfig wake{
        .melspectrogram_path = models + "/melspectrogram.onnx",
        .embedding_path = models + "/embedding_model.onnx",
        .model_path = models + "/" + env_string("BUDDY_WAKE_MODEL", "hey_jarvis_v0.1.onnx"),
        .threshold = env_float("BUDDY_WAKE_THRESHOLD", 0.5f),
        .patience = env_int("BUDDY_WAKE_PATIENCE", 2),
    };
    std::string vad_path = models + "/silero_vad.onnx";
    EndpointConfig endpoint{
        .threshold = env_float("BUDDY_VAD_THRESHOLD", 0.5f),
        .silence_ms = env_int("BUDDY_VAD_SILENCE_MS", 700),
        .start_timeout_ms = env_int("BUDDY_VAD_START_TIMEOUT_MS", 4000),
        .max_ms = env_int("BUDDY_VAD_MAX_MS", 5000),
        .level_gate = env_float("BUDDY_VAD_LEVEL_GATE", 0.4f),
    };
    SttConfig stt{
        .model_path = models + "/" + env_string("BUDDY_STT_MODEL", "ggml-base.en.bin"),
        .language = env_string("BUDDY_STT_LANGUAGE", "en"),
        .threads = env_int("BUDDY_STT_THREADS", 4),
        .prompt = env_string("BUDDY_STT_PROMPT", ""),
    };
};

long ms_since(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

// The actual pipeline. Returns when the audio source ends or on shutdown.
// speaking: true while voice talks (buddy/voice/state); nullptr offline.
void run(const Config& cfg, AudioSource& source, WakeWord& wake, Endpointer& endpointer, Stt& stt,
         const Publish& publish, const std::atomic<bool>* speaking) {
    enum class State { Idle, Listening } state = State::Idle;
    auto set_state = [&](const char* s) { publish(topic::ears_state, {{"state", s}}, true); };

    std::vector<int16_t> command;
    LevelHistory levels;
    Chunk chunk;
    long chunks = 0;  // audio position, for logs: chunks × 80 ms
    auto pos = [&] { return std::to_string(chunks * 80 / 1000.0).substr(0, 5) + " s"; };

    set_state("idle");
    std::cout << "[ears] listening for \"" << wake.name() << "\"" << std::endl;

    while (running() && source.read(chunk)) {
        ++chunks;

        if (state == State::Idle) {
            levels.push(chunk);
            const bool heard = wake.process(chunk);
            if (cfg.debug && wake.last_score() >= 0.1f)
                std::cout << "[ears] " << pos() << " score " << wake.last_score() << std::endl;
            if (!heard) continue;
            if (cfg.ignore_while_speaking && speaking && *speaking) {
                std::cout << "[ears] " << pos() << " wake word ignored: Buddy is speaking" << std::endl;
                wake.reset();
                continue;
            }

            std::cout << "[ears] " << pos() << " wake word (score " << wake.last_score() << ")" << std::endl;
            publish(topic::ears_wake, {{"model", wake.name()}, {"score", wake.last_score()}}, false);
            set_state("listening");
            wake.reset();
            const LevelGate gate = levels.gate(endpointer.config().level_gate);
            levels.clear();  // the next wake word gets a fresh background, not this command
            std::cout << "[ears] levels: ";
            if (gate.measured())
                std::cout << "background " << std::lround(gate.background_db) << " dB, voice "
                          << std::lround(gate.voice_db) << " dB, ";
            if (gate.active()) std::cout << "gate " << std::lround(gate.gate_db) << " dB" << std::endl;
            else std::cout << "gate off" << std::endl;
            endpointer.start(gate);
            command.clear();
            state = State::Listening;
            continue;
        }

        command.insert(command.end(), chunk.begin(), chunk.end());
        const auto result = endpointer.process(chunk);
        if (result == Endpointer::Result::Continue) continue;

        if (result == Endpointer::Result::NoSpeech) {
            std::cout << "[ears] " << pos() << " no command after wake word" << std::endl;
        } else {
            if (result == Endpointer::Result::TooLong)
                std::cout << "[ears] " << pos() << " command cut at " << endpointer.config().max_ms << " ms"
                          << std::endl;
            set_state("transcribing");
            const auto t0 = Clock::now();
            try {
                const Transcript t = stt.transcribe(command);
                const long stt_ms = ms_since(t0);
                const long audio_ms = static_cast<long>(command.size() * 1000 / kSampleRate);
                std::cout << "[ears] " << pos() << " \"" << t.text << "\" (" << audio_ms << " ms audio, stt "
                          << stt_ms << " ms)" << std::endl;
                if (!t.text.empty()) {
                    publish(topic::ears_transcript,
                            {{"text", t.text}, {"language", t.language}, {"audio_ms", audio_ms}, {"stt_ms", stt_ms}},
                            false);
                }
            } catch (const std::exception& e) {
                std::cout << "[ears] transcription failed: " << e.what() << std::endl;
            }
        }
        set_state("idle");
        state = State::Idle;
    }
}

// Speech-to-text on whole files, no wake word or VAD. Same model, language and prompt as live.
int transcribe(Stt& stt, int argc, char** argv) {
    for (int i = 2; i < argc; ++i) {
        try {
            WavSource wav(argv[i]);
            std::vector<int16_t> audio;
            Chunk chunk;
            while (wav.read(chunk)) audio.insert(audio.end(), chunk.begin(), chunk.end());
            const auto t0 = Clock::now();
            const Transcript t = stt.transcribe(audio);
            std::cout << argv[i] << "\t" << ms_since(t0) << "\t" << t.text << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[ears] " << argv[i] << ": " << e.what() << std::endl;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();

    const char* wav = nullptr;
    const bool stt_only = argc >= 3 && std::strcmp(argv[1], "--transcribe") == 0;
    if (argc == 3 && std::strcmp(argv[1], "--wav") == 0) wav = argv[2];
    else if (argc != 1 && !stt_only) {
        std::cerr << "usage: " << argv[0] << " [--wav file.wav | --transcribe file.wav…]" << std::endl;
        return 2;
    }

    if (stt_only) {
        try {
            const Config cfg;
            Stt stt(cfg.stt);
            return transcribe(stt, argc, argv);
        } catch (const std::exception& e) {
            std::cerr << "[ears] " << e.what() << std::endl;
            return 1;
        }
    }

    // Everything that can fail on bad config or missing models fails here, before touching MQTT.
    std::optional<Config> cfg;
    std::unique_ptr<AudioSource> source;
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "buddy-ears");
    std::optional<WakeWord> wake;
    std::optional<SileroVad> vad;
    std::optional<Stt> stt;
    try {
        cfg.emplace();
        wake.emplace(env, cfg->wake);
        vad.emplace(env, cfg->vad_path);
        const auto t0 = Clock::now();
        stt.emplace(cfg->stt);
        std::cout << "[ears] loaded " << cfg->stt.model_path << " in " << ms_since(t0) << " ms" << std::endl;
        if (wav) source = std::make_unique<WavSource>(wav);
        else source = std::make_unique<AlsaSource>(cfg->device);
    } catch (const std::exception& e) {
        std::cerr << "[ears] " << e.what() << std::endl;
        return 1;
    }
    Endpointer endpointer(*vad, cfg->endpoint);

    if (wav) {
        const Publish print = [](std::string_view topic, const nlohmann::json& msg, bool) {
            std::cout << "  → " << topic << " " << msg.dump() << std::endl;
        };
        run(*cfg, *source, *wake, endpointer, *stt, print, nullptr);
        return 0;
    }

    std::atomic<bool> speaking{false};
    Mqtt mqtt("ears", mqtt_config_from_env());
    mqtt.subscribe(topic::voice_state, [&](const std::string&, const std::string& payload) {
        const auto msg = nlohmann::json::parse(payload, nullptr, /*allow_exceptions=*/false);
        speaking = msg.is_object() && msg.value("state", "") == "speaking";
    });
    if (!mqtt.start()) return 0;
    const Publish publish = [&mqtt](std::string_view topic, const nlohmann::json& msg, bool retained) {
        mqtt.publish(topic, msg.dump(), 0, retained);
    };
    run(*cfg, *source, *wake, endpointer, *stt, publish, &speaking);

    publish(topic::ears_state, {{"state", "offline"}}, true);
    mqtt.stop();
    return 0;
}
