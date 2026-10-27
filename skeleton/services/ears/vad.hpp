#pragma once
// Silero VAD (v5/v6 ONNX): speech probability per 32 ms frame. The end-of-command logic is in endpoint.hpp.

#include <array>
#include <span>
#include <string>

#include "endpoint.hpp"
#include "onnx_model.hpp"

namespace buddy::ears {

class SileroVad final : public SpeechModel {
public:
    SileroVad(Ort::Env& env, const std::string& model_path);

    float process(std::span<const float> frame) override;
    void reset() override;

private:
    static constexpr std::size_t kContext = 64;  // samples of the previous frame the model expects in front

    OnnxModel model_;
    std::array<float, 2 * 1 * 128> state_{};
    std::array<float, kContext> context_{};
};

} // namespace buddy::ears
