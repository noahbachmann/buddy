#pragma once
// Text-to-speech with Piper (libpiper). The voice model is loaded once at start-up.
// Piper synthesizes sentence by sentence; each sentence is handed to the callback as soon as it's
// ready, so the first sentence can play while the next is computed.

#include <cstdint>
#include <functional>
#include <span>
#include <string>

struct piper_synthesizer;

namespace buddy::voice {

struct TtsConfig {
    std::string model_path;        // e.g. en_US-lessac-medium.onnx (+ .onnx.json next to it)
    std::string espeak_data_path;  // espeak-ng-data installed with libpiper
    float length_scale = 1.0f;     // > 1 slower, < 1 faster
    float gain = 1.0f;             // applied before converting to 16-bit
    int threads = 3;               // ONNX threads; needs setup.sh's patched libpiper, stock libpiper uses 1
};

class Tts {
public:
    // samples: 16-bit mono at sample_rate
    using Sink = std::function<void(std::span<const int16_t> samples, int sample_rate)>;

    explicit Tts(TtsConfig cfg);  // throws if the voice can't be loaded
    ~Tts();
    Tts(const Tts&) = delete;
    Tts& operator=(const Tts&) = delete;

    void speak(const std::string& text, const Sink& sink);  // throws on synthesis errors

private:
    TtsConfig cfg_;
    piper_synthesizer* synth_ = nullptr;
};

} // namespace buddy::voice
