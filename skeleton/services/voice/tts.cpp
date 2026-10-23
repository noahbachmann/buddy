#include "tts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <piper.h>

namespace buddy::voice {

Tts::Tts(TtsConfig cfg) : cfg_(std::move(cfg)) {
    if (!std::filesystem::exists(cfg_.model_path)) throw std::runtime_error("voice model not found: " + cfg_.model_path);
    if (!std::filesystem::exists(cfg_.model_path + ".json"))
        throw std::runtime_error("voice config not found: " + cfg_.model_path + ".json");
    if (!std::filesystem::is_directory(cfg_.espeak_data_path))
        throw std::runtime_error("espeak-ng data not found: " + cfg_.espeak_data_path);
    // libpiper has no API for this; setup-pi.sh's patched build reads it in piper_create
    setenv("PIPER_INTRA_OP_THREADS", std::to_string(cfg_.threads).c_str(), 1);
    synth_ = piper_create(cfg_.model_path.c_str(), nullptr, cfg_.espeak_data_path.c_str());
    if (!synth_) throw std::runtime_error("cannot load voice " + cfg_.model_path);
}

Tts::~Tts() { piper_free(synth_); }

void Tts::speak(const std::string& text, const Sink& sink) {
    piper_synthesize_options options = piper_default_synthesize_options(synth_);
    options.length_scale *= cfg_.length_scale;
    if (piper_synthesize_start(synth_, text.c_str(), &options) != PIPER_OK)
        throw std::runtime_error("piper_synthesize_start failed");

    std::vector<int16_t> pcm;
    piper_audio_chunk chunk;
    while (true) {
        const int rc = piper_synthesize_next(synth_, &chunk);
        if (rc != PIPER_OK && rc != PIPER_DONE) throw std::runtime_error("piper_synthesize_next failed");
        if (chunk.num_samples > 0) {
            pcm.resize(chunk.num_samples);
            for (size_t i = 0; i < chunk.num_samples; ++i) {
                const float s = std::clamp(chunk.samples[i] * cfg_.gain, -1.0f, 1.0f);
                pcm[i] = static_cast<int16_t>(std::lround(s * 32767.0f));
            }
            sink(pcm, chunk.sample_rate);
        }
        if (rc == PIPER_DONE || chunk.is_last) break;
    }
}

} // namespace buddy::voice
