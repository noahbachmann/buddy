#include "vad.hpp"

#include <algorithm>
#include <stdexcept>

#include "audio_source.hpp"

namespace buddy::ears {

namespace {
constexpr int kFrameMs = static_cast<int>(SileroVad::kFrameSamples * 1000 / kSampleRate);  // 32
constexpr int kMinSpeechMs = 96;  // ignore clicks: this much speech before a command counts as started
} // namespace

SileroVad::SileroVad(Ort::Env& env, const std::string& model_path) : model_(env, model_path) {}

void SileroVad::reset() {
    state_.fill(0.0f);
    context_.fill(0.0f);
}

float SileroVad::process(std::span<const float> frame) {
    if (frame.size() != kFrameSamples) throw std::logic_error("SileroVad::process: wrong frame size");

    std::array<float, kContext + kFrameSamples> input;
    std::copy(context_.begin(), context_.end(), input.begin());
    std::copy(frame.begin(), frame.end(), input.begin() + kContext);
    std::copy(frame.end() - kContext, frame.end(), context_.begin());

    int64_t sr = kSampleRate;
    const std::array<int64_t, 2> input_shape{1, static_cast<int64_t>(input.size())};
    const std::array<int64_t, 3> state_shape{2, 1, 128};
    std::vector<Ort::Value> in;  // model order: input, state, sr
    in.push_back(Ort::Value::CreateTensor<float>(OnnxModel::cpu(), input.data(), input.size(), input_shape.data(),
                                                 input_shape.size()));
    in.push_back(Ort::Value::CreateTensor<float>(OnnxModel::cpu(), state_.data(), state_.size(), state_shape.data(),
                                                 state_shape.size()));
    in.push_back(Ort::Value::CreateTensor<int64_t>(OnnxModel::cpu(), &sr, 1, nullptr, 0));

    auto out = model_.run(in);  // output (1,1), stateN (2,1,128)
    const float* new_state = out[1].GetTensorData<float>();
    std::copy(new_state, new_state + state_.size(), state_.begin());
    return out[0].GetTensorData<float>()[0];
}

void Endpointer::start() {
    vad_.reset();
    pending_.clear();
    in_speech_ = false;
    elapsed_ms_ = speech_ms_ = silence_ms_ = 0;
}

Endpointer::Result Endpointer::process(std::span<const int16_t> chunk) {
    for (int16_t s : chunk) pending_.push_back(static_cast<float>(s) / 32768.0f);

    std::size_t used = 0;
    for (; used + SileroVad::kFrameSamples <= pending_.size(); used += SileroVad::kFrameSamples) {
        const float p = vad_.process(std::span(pending_).subspan(used, SileroVad::kFrameSamples));
        elapsed_ms_ += kFrameMs;

        // Hysteresis as in Silero's get_speech_timestamps: enter at threshold, leave below threshold − 0.15.
        if (p >= cfg_.threshold) in_speech_ = true;
        else if (p < cfg_.threshold - 0.15f) in_speech_ = false;

        if (in_speech_) {
            speech_ms_ += kFrameMs;
            silence_ms_ = 0;
        } else {
            silence_ms_ += kFrameMs;
        }
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(used));

    const bool started = speech_ms_ >= kMinSpeechMs;
    if (started && silence_ms_ >= cfg_.silence_ms) return Result::Done;
    if (started && elapsed_ms_ >= cfg_.max_ms) return Result::Done;
    if (!started && elapsed_ms_ >= cfg_.start_timeout_ms) return Result::NoSpeech;
    return Result::Continue;
}

} // namespace buddy::ears
