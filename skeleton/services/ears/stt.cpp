#include "stt.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <whisper.h>

#include "audio_source.hpp"

namespace buddy::ears {

namespace {

// whisper.cpp logs a lot at INFO level. Keep only warnings and errors (microSD wear, readable journal).
void quiet_log(ggml_log_level level, const char* text, void*) {
    if (level == GGML_LOG_LEVEL_WARN || level == GGML_LOG_LEVEL_ERROR) std::cout << "[whisper] " << text << std::flush;
}

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\n");
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(" \t\n") - first + 1);
}

} // namespace

Stt::Stt(SttConfig cfg) : cfg_(std::move(cfg)) {
    whisper_log_set(quiet_log, nullptr);
    auto params = whisper_context_default_params();
    params.use_gpu = false;
    ctx_ = whisper_init_from_file_with_params(cfg_.model_path.c_str(), params);
    if (!ctx_) throw std::runtime_error("cannot load whisper model " + cfg_.model_path);
    if (!whisper_is_multilingual(ctx_) && cfg_.language != "en") {
        std::cout << "[ears] " << cfg_.model_path << " is English-only, ignoring language \"" << cfg_.language
                  << "\"" << std::endl;
        cfg_.language = "en";
    }
}

Stt::~Stt() {
    if (ctx_) whisper_free(ctx_);
}

Transcript Stt::transcribe(std::span<const int16_t> audio) {
    // whisper.cpp skips input shorter than 1 s, so pad short commands ("pause") with silence.
    std::vector<float> pcm(std::max<std::size_t>(audio.size(), kSampleRate * 11 / 10), 0.0f);
    for (std::size_t i = 0; i < audio.size(); ++i) pcm[i] = static_cast<float>(audio[i]) / 32768.0f;

    auto params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads = cfg_.threads;
    params.language = cfg_.language.c_str();
    params.detect_language = false;
    params.no_context = true;
    params.no_timestamps = true;
    params.single_segment = true;
    params.suppress_nst = true;  // drop "[music]", "(coughs)" and similar
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.print_special = false;
    if (!cfg_.prompt.empty()) params.initial_prompt = cfg_.prompt.c_str();

    if (whisper_full(ctx_, params, pcm.data(), static_cast<int>(pcm.size())) != 0)
        throw std::runtime_error("whisper_full failed");

    Transcript result;
    for (int i = 0; i < whisper_full_n_segments(ctx_); ++i) result.text += whisper_full_get_segment_text(ctx_, i);
    result.text = trim(result.text);
    result.language = whisper_lang_str(whisper_full_lang_id(ctx_));
    return result;
}

} // namespace buddy::ears
