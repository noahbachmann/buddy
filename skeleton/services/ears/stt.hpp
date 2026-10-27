#pragma once
// Speech-to-text with whisper.cpp. The model is loaded once at start-up and kept in memory.

#include <cstdint>
#include <span>
#include <string>

struct whisper_context;

namespace buddy::ears {

struct SttConfig {
    std::string model_path;      // e.g. ggml-base.en.bin
    std::string language = "en"; // "en", "de" or "auto" (".en" models: English only)
    int threads = 4;
    std::string prompt;          // optional vocabulary hint, e.g. "Spotify, Buddy"
    int audio_ctx = 0;           // encoder window: 0 = full 30 s, kAutoAudioCtx = sized to the audio, N = fixed
    bool fallback = false;       // re-decode at higher temperature when unsure (whisper default): no accuracy gain on
                                 // Buddy's commands, but up to 11 s worst case (eval 2026-10-25)
};

// Encoder frames for `samples` of 16 kHz audio with audio_ctx = auto: 50 per second (whisper: 1500 for 30 s) plus a
// margin. tools/voice/buddy_voice/audio.py uses the same formula to train short-context models; both are checked
// against shared/tests/cases/audio_ctx.json.
inline constexpr int kAutoAudioCtx = -1;
inline constexpr int kAudioCtxMargin = 16;
inline int auto_audio_ctx(std::size_t samples) {
    const int frames = static_cast<int>((samples + 319) / 320) + kAudioCtxMargin;
    return frames < 1500 ? frames : 1500;
}

struct Transcript {
    std::string text;
    std::string language;
};

class Stt {
public:
    explicit Stt(SttConfig cfg);  // throws if the model can't be loaded
    ~Stt();
    Stt(const Stt&) = delete;
    Stt& operator=(const Stt&) = delete;

    Transcript transcribe(std::span<const int16_t> audio);

private:
    SttConfig cfg_;
    whisper_context* ctx_ = nullptr;
};

} // namespace buddy::ears
