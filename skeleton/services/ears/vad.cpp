#include "vad.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "audio_source.hpp"

namespace buddy::ears {

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

} // namespace buddy::ears
