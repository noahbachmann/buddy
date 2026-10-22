#pragma once
// Where ears gets its audio from: always 16 kHz, mono, signed 16-bit, in chunks of kChunkSamples.
//   AlsaSource – live microphone via the ALSA "default" device (= PipeWire through pipewire-alsa)
//   WavSource  – a WAV file, for offline tests (buddy-ears --wav file.wav)

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace buddy::ears {

inline constexpr int kSampleRate = 16000;
inline constexpr std::size_t kChunkSamples = 1280;  // 80 ms: openWakeWord's step size

using Chunk = std::vector<int16_t>;

class AudioSource {
public:
    virtual ~AudioSource() = default;
    // Blocks until a full chunk is available. Returns false at end of input or on shutdown.
    virtual bool read(Chunk& chunk) = 0;
};

// Captures on a background thread so audio keeps flowing while whisper is busy.
// Chunks queue up meanwhile (up to max_queue_ms, oldest dropped after that) and are
// processed afterwards, so "hey buddy" said during a transcription is still heard.
class AlsaSource : public AudioSource {
public:
    explicit AlsaSource(std::string device, int max_queue_ms = 10000);
    ~AlsaSource() override;
    bool read(Chunk& chunk) override;

private:
    void capture_loop();

    std::string device_;
    std::size_t max_chunks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Chunk> queue_;
    bool stopped_ = false;
    std::thread thread_;
};

class WavSource : public AudioSource {
public:
    explicit WavSource(const std::string& path);  // throws on unsupported format
    bool read(Chunk& chunk) override;

private:
    std::ifstream file_;
    std::size_t remaining_ = 0;  // bytes of sample data left
    int trailing_silence_chunks_ = 25;  // 2 s, so VAD sees the end of the last utterance
};

} // namespace buddy::ears
