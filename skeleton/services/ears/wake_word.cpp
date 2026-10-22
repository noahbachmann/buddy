#include "wake_word.hpp"

#include <array>
#include <filesystem>
#include <stdexcept>

#include "audio_source.hpp"

namespace buddy::ears {

namespace {

constexpr std::size_t kMelBins = 32;
constexpr std::size_t kMelWindow = 76;    // mel frames per embedding (775 ms)
constexpr std::size_t kFeatureDim = 96;
constexpr std::size_t kFeatureWindow = 16;// embeddings per wake word prediction (1.28 s)
constexpr std::size_t kRawOverlap = 480;  // 3 × 160-sample hop, as in openWakeWord

} // namespace

WakeWord::WakeWord(Ort::Env& env, WakeWordConfig cfg)
    : cfg_(std::move(cfg)),
      name_(std::filesystem::path(cfg_.model_path).stem().string()),
      mel_(env, cfg_.melspectrogram_path),
      embedding_(env, cfg_.embedding_path),
      model_(env, cfg_.model_path) {
    reset();
}

void WakeWord::reset() {
    raw_tail_.assign(kRawOverlap, 0.0f);
    // openWakeWord starts with a buffer of ones, so embeddings are available right away.
    mels_.assign(kMelWindow, std::vector<float>(kMelBins, 1.0f));
    features_.clear();
    last_score_ = 0.0f;
    above_ = 0;
}

bool WakeWord::process(std::span<const int16_t> chunk) {
    if (chunk.size() != kChunkSamples) throw std::logic_error("WakeWord::process: wrong chunk size");

    // 1. Mel spectrogram. Input is the raw int16 values as float (not scaled to ±1).
    std::vector<float> raw(raw_tail_);
    raw.insert(raw.end(), chunk.begin(), chunk.end());
    raw_tail_.assign(raw.end() - kRawOverlap, raw.end());

    const std::array<int64_t, 2> raw_shape{1, static_cast<int64_t>(raw.size())};
    std::vector<Ort::Value> mel_in;
    mel_in.push_back(Ort::Value::CreateTensor<float>(OnnxModel::cpu(), raw.data(), raw.size(), raw_shape.data(),
                                                     raw_shape.size()));
    auto mel_out = mel_.run(mel_in);
    const float* mel = mel_out[0].GetTensorData<float>();
    const std::size_t mel_values = mel_out[0].GetTensorTypeAndShapeInfo().GetElementCount();  // (1,1,frames,32)
    for (std::size_t f = 0; f + kMelBins <= mel_values; f += kMelBins) {
        std::vector<float> frame(kMelBins);
        for (std::size_t b = 0; b < kMelBins; ++b) frame[b] = mel[f + b] / 10.0f + 2.0f;  // match Google's model
        mels_.push_back(std::move(frame));
    }
    while (mels_.size() > kMelWindow) mels_.pop_front();

    // 2. Speech embedding over the last 76 mel frames, shape (1, 76, 32, 1).
    std::vector<float> window;
    window.reserve(kMelWindow * kMelBins);
    for (const auto& frame : mels_) window.insert(window.end(), frame.begin(), frame.end());
    const std::array<int64_t, 4> emb_shape{1, kMelWindow, kMelBins, 1};
    std::vector<Ort::Value> emb_in;
    emb_in.push_back(Ort::Value::CreateTensor<float>(OnnxModel::cpu(), window.data(), window.size(),
                                                     emb_shape.data(), emb_shape.size()));
    auto emb_out = embedding_.run(emb_in);
    const float* emb = emb_out[0].GetTensorData<float>();
    features_.emplace_back(emb, emb + kFeatureDim);
    while (features_.size() > kFeatureWindow) features_.pop_front();
    if (features_.size() < kFeatureWindow) return false;  // first 1.28 s after start/reset

    // 3. Wake word classifier over the last 16 embeddings, shape (1, 16, 96).
    std::vector<float> feats;
    feats.reserve(kFeatureWindow * kFeatureDim);
    for (const auto& f : features_) feats.insert(feats.end(), f.begin(), f.end());
    const std::array<int64_t, 3> ww_shape{1, kFeatureWindow, kFeatureDim};
    std::vector<Ort::Value> ww_in;
    ww_in.push_back(Ort::Value::CreateTensor<float>(OnnxModel::cpu(), feats.data(), feats.size(), ww_shape.data(),
                                                    ww_shape.size()));
    auto ww_out = model_.run(ww_in);
    last_score_ = ww_out[0].GetTensorData<float>()[0];

    above_ = last_score_ >= cfg_.threshold ? above_ + 1 : 0;
    return above_ >= cfg_.patience;
}

} // namespace buddy::ears
