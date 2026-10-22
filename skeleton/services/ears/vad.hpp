#pragma once
// Silero VAD (v5/v6 ONNX): speech probability per 32 ms frame, plus end-of-speech logic.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "onnx_model.hpp"

namespace buddy::ears {

class SileroVad {
public:
    static constexpr std::size_t kFrameSamples = 512;  // 32 ms at 16 kHz, fixed by the model

    SileroVad(Ort::Env& env, const std::string& model_path);

    // Exactly kFrameSamples → speech probability 0..1. The model is stateful: call reset() between utterances.
    float process(std::span<const float> frame);
    void reset();

private:
    static constexpr std::size_t kContext = 64;  // samples of the previous frame the model expects in front

    OnnxModel model_;
    std::array<float, 2 * 1 * 128> state_{};
    std::array<float, kContext> context_{};
};

struct EndpointConfig {
    float threshold = 0.5f;     // prob ≥ this = speech
    int silence_ms = 700;       // this much silence after speech = end of command
    int start_timeout_ms = 4000;// no speech this long after the wake word = give up
    int max_ms = 10000;         // hard cap on command length
};

// Decides when a command is over. Feed it 80 ms chunks after the wake word.
class Endpointer {
public:
    enum class Result { Continue, Done, NoSpeech };

    Endpointer(SileroVad& vad, EndpointConfig cfg) : vad_(vad), cfg_(cfg) {}

    void start();
    Result process(std::span<const int16_t> chunk);

    int speech_ms() const { return speech_ms_; }

private:
    SileroVad& vad_;
    EndpointConfig cfg_;
    std::vector<float> pending_;
    bool in_speech_ = false;
    int elapsed_ms_ = 0, speech_ms_ = 0, silence_ms_ = 0;
};

} // namespace buddy::ears
