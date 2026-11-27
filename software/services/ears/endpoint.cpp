#include "endpoint.hpp"

#include <algorithm>
#include <functional>

#include "audio_source.hpp"

namespace buddy::ears {

namespace {
constexpr int kFrameMs = static_cast<int>(SpeechModel::kFrameSamples * 1000 / kSampleRate);  // 32
constexpr int kMinSpeechMs = 96;  // ignore clicks: this much speech before a command counts as started

// Level history, in 80 ms chunks. The wake word fires ~0.2 s after "Hey Jarvis" ends, so the phrase is in the
// last 1.2 s; the 2 s before that are background. Needs 3.2 s of history.
constexpr std::size_t kVoiceChunks = 15;
constexpr std::size_t kBackgroundChunks = 25;
constexpr std::size_t kLoudestChunks = 4;  // user level = mean of the loudest 320 ms of the wake word
constexpr float kMinContrastDb = 6.0f;     // user less than this above background: can't separate, gate off

// Mean square of ±1 floats → dBFS. The +1e-9 floor keeps digital silence at −90 dB instead of −inf.
float to_db(double sum_squares, std::size_t n) {
    return 10.0f * std::log10(static_cast<float>(sum_squares / static_cast<double>(n)) + 1e-9f);
}

float level_db(std::span<const float> samples) {
    double sum = 0;
    for (float s : samples) sum += static_cast<double>(s) * s;
    return to_db(sum, samples.size());
}
} // namespace

float level_db(std::span<const int16_t> samples) {
    double sum = 0;
    for (int16_t s : samples) {
        const double x = s / 32768.0;
        sum += x * x;
    }
    return to_db(sum, samples.size());
}

void LevelHistory::push(std::span<const int16_t> chunk) {
    levels_.push_back(level_db(chunk));
    while (levels_.size() > kVoiceChunks + kBackgroundChunks) levels_.pop_front();
}

LevelGate LevelHistory::gate(float position) const {
    LevelGate g;
    if (levels_.size() < kVoiceChunks + kBackgroundChunks) return g;

    // Median, not mean: TV speech has pauses and peaks, the median is its typical level.
    std::vector<float> background(levels_.begin(), levels_.end() - kVoiceChunks);
    std::nth_element(background.begin(), background.begin() + background.size() / 2, background.end());
    g.background_db = background[background.size() / 2];

    std::vector<float> voice(levels_.end() - kVoiceChunks, levels_.end());
    std::partial_sort(voice.begin(), voice.begin() + kLoudestChunks, voice.end(), std::greater<>());
    float sum = 0;
    for (std::size_t i = 0; i < kLoudestChunks; ++i) sum += voice[i];
    g.voice_db = sum / kLoudestChunks;

    if (position > 0 && g.voice_db - g.background_db >= kMinContrastDb)
        g.gate_db = g.background_db + position * (g.voice_db - g.background_db);
    return g;
}

void Endpointer::start(const LevelGate& gate) {
    vad_.reset();
    gate_db_ = gate.gate_db;
    pending_.clear();
    in_speech_ = false;
    elapsed_ms_ = speech_ms_ = silence_ms_ = 0;
}

Endpointer::Result Endpointer::process(std::span<const int16_t> chunk) {
    for (int16_t s : chunk) pending_.push_back(static_cast<float>(s) / 32768.0f);

    std::size_t used = 0;
    for (; used + SpeechModel::kFrameSamples <= pending_.size(); used += SpeechModel::kFrameSamples) {
        const auto frame = std::span<const float>(pending_).subspan(used, SpeechModel::kFrameSamples);
        const float p = vad_.process(frame);
        elapsed_ms_ += kFrameMs;

        // Hysteresis as in Silero's get_speech_timestamps: enter at threshold, leave below threshold − 0.15.
        if (p >= cfg_.threshold) in_speech_ = true;
        else if (p < cfg_.threshold - 0.15f) in_speech_ = false;

        // Speech below the gate is the background talking (TV), not the user: counts as silence.
        if (in_speech_ && level_db(frame) >= gate_db_) {
            speech_ms_ += kFrameMs;
            silence_ms_ = 0;
        } else {
            silence_ms_ += kFrameMs;
        }
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(used));

    const bool started = speech_ms_ >= kMinSpeechMs;
    if (started && silence_ms_ >= cfg_.silence_ms) return Result::Done;
    if (started && elapsed_ms_ >= cfg_.max_ms) return Result::TooLong;
    if (!started && elapsed_ms_ >= cfg_.start_timeout_ms) return Result::NoSpeech;
    return Result::Continue;
}

} // namespace buddy::ears
