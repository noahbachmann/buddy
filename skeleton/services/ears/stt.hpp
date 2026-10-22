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
};

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
