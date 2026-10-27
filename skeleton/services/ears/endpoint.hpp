#pragma once
// End-of-command logic on top of a speech model (Silero in ears, a script in the tests), plus the level gate
// that ignores background speech quieter than the user (TV). No ONNX: tested without models.

#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <span>
#include <vector>

namespace buddy::ears {

// Speech probability per 32 ms frame.
class SpeechModel {
public:
    static constexpr std::size_t kFrameSamples = 512;  // 32 ms at 16 kHz, fixed by Silero

    virtual ~SpeechModel() = default;
    // Exactly kFrameSamples → speech probability 0..1. Stateful: reset() between utterances.
    virtual float process(std::span<const float> frame) = 0;
    virtual void reset() = 0;
};

// Loudness in dBFS: 0 = full scale, digital silence ≈ −90.
float level_db(std::span<const int16_t> samples);

// Silero says "speech" for any voice, including a TV. To tell the user from background speech, ears compares
// loudness: the wake word itself shows how loud the user is, the seconds before it how loud the background is.
// During the command, only frames louder than a gate between the two count as the user speaking.
struct LevelGate {
    float background_db = std::numeric_limits<float>::quiet_NaN();  // NaN = not enough history yet
    float voice_db = std::numeric_limits<float>::quiet_NaN();
    float gate_db = -std::numeric_limits<float>::infinity();  // −inf = gate off
    bool measured() const { return !std::isnan(voice_db); }
    bool active() const { return std::isfinite(gate_db); }
};

class LevelHistory {
public:
    void push(std::span<const int16_t> chunk);  // every 80 ms chunk while waiting for the wake word
    void clear() { levels_.clear(); }

    // Call right after the wake word. position: 0 = gate at background level, 1 = at the user's level.
    // Gate off if there is too little history (just started) or the user isn't clearly louder than the background.
    LevelGate gate(float position) const;

private:
    std::deque<float> levels_;  // dB per chunk, newest last
};

struct EndpointConfig {
    float threshold = 0.5f;     // prob ≥ this = speech
    int silence_ms = 700;       // this much silence after speech = end of command
    int start_timeout_ms = 4000;// no speech this long after the wake word = give up
    int max_ms = 5000;          // hard cap on command length
    float level_gate = 0.4f;    // LevelHistory::gate position; 0 = off
};

// Decides when a command is over. Feed it 80 ms chunks after the wake word.
class Endpointer {
public:
    enum class Result { Continue, Done, TooLong, NoSpeech };

    Endpointer(SpeechModel& vad, EndpointConfig cfg) : vad_(vad), cfg_(cfg) {}

    const EndpointConfig& config() const { return cfg_; }

    void start(const LevelGate& gate = {});
    Result process(std::span<const int16_t> chunk);

    int speech_ms() const { return speech_ms_; }

private:
    SpeechModel& vad_;
    EndpointConfig cfg_;
    float gate_db_ = -std::numeric_limits<float>::infinity();
    std::vector<float> pending_;
    bool in_speech_ = false;
    int elapsed_ms_ = 0, speech_ms_ = 0, silence_ms_ = 0;
};

} // namespace buddy::ears
