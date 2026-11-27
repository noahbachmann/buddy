#pragma once
// openWakeWord in C++: three ONNX models in a row, fed one 80 ms chunk at a time.
//
//   1280 samples ─► melspectrogram.onnx ─► 8 mel frames (32 bins)
//   last 76 mel frames ─► embedding_model.onnx ─► 1 feature vector (96)
//   last 16 feature vectors ─► <wake word>.onnx ─► score 0..1
//
// Mirrors openwakeword/utils.py (AudioFeatures._streaming_features) and model.py.

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

#include "onnx_model.hpp"

namespace buddy::ears {

struct WakeWordConfig {
    std::string melspectrogram_path;
    std::string embedding_path;
    std::string model_path;   // e.g. hey_jarvis_v0.1.onnx
    float threshold = 0.5f;   // score that counts as "heard"
    int patience = 2;         // consecutive chunks ≥ threshold needed to trigger
};

class WakeWord {
public:
    WakeWord(Ort::Env& env, WakeWordConfig cfg);

    // Feed exactly kChunkSamples. Returns true when the wake word was detected.
    bool process(std::span<const int16_t> chunk);

    float last_score() const { return last_score_; }
    const std::string& name() const { return name_; }

    // Forget recent audio, e.g. after a detection so the same utterance doesn't trigger twice.
    void reset();

private:
    WakeWordConfig cfg_;
    std::string name_;
    OnnxModel mel_, embedding_, model_;

    std::vector<float> raw_tail_;            // last 480 samples: mel frames overlap chunk boundaries
    std::deque<std::vector<float>> mels_;    // newest last, 32 floats each
    std::deque<std::vector<float>> features_;// newest last, 96 floats each
    float last_score_ = 0.0f;
    int above_ = 0;
};

} // namespace buddy::ears
