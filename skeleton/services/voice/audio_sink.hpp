#pragma once
// Where voice sends its audio: 16-bit mono at the voice's sample rate (22050 Hz for "medium" voices).
//   AlsaSink – the ALSA "default" device (= PipeWire through pipewire-alsa); PipeWire resamples and mixes
//              with Spotify
//   WavSink  – a WAV file, for offline tests (buddy-voice --say "…" --wav out.wav)

#include <cstdint>
#include <span>
#include <string>
#include <vector>

typedef struct _snd_pcm snd_pcm_t;

namespace buddy::voice {

class AudioSink {
public:
    virtual ~AudioSink() = default;
    // One utterance: begin → write… → end. begin() sees the first chunk's sample rate.
    virtual void begin(int sample_rate) = 0;
    virtual void write(std::span<const int16_t> samples) = 0;
    virtual void end() = 0;  // blocks until everything was played
};

class AlsaSink : public AudioSink {
public:
    explicit AlsaSink(std::string device) : device_(std::move(device)) {}
    ~AlsaSink() override;
    void begin(int sample_rate) override;  // throws if the device can't be opened
    void write(std::span<const int16_t> samples) override;
    void end() override;

private:
    std::string device_;
    snd_pcm_t* pcm_ = nullptr;
};

class WavSink : public AudioSink {
public:
    explicit WavSink(std::string path) : path_(std::move(path)) {}
    void begin(int sample_rate) override;
    void write(std::span<const int16_t> samples) override;
    void end() override;  // writes the file

private:
    std::string path_;
    int sample_rate_ = 0;
    std::vector<int16_t> samples_;
};

} // namespace buddy::voice
