#include "audio_sink.hpp"

#include <fstream>
#include <stdexcept>

#include <alsa/asoundlib.h>

namespace buddy::voice {

// ---------------------------------------------------------------- ALSA

AlsaSink::~AlsaSink() {
    if (pcm_) snd_pcm_close(pcm_);
}

void AlsaSink::begin(int sample_rate) {
    // Opened per utterance, not held open: an idle open stream would keep the Jabra's sink busy
    // and PipeWire from suspending it.
    int err = snd_pcm_open(&pcm_, device_.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err == 0) {
        err = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                 /*channels=*/1, static_cast<unsigned>(sample_rate), /*soft_resample=*/1,
                                 /*latency_us=*/200000);
    }
    if (err < 0) {
        if (pcm_) snd_pcm_close(pcm_);
        pcm_ = nullptr;
        throw std::runtime_error("cannot open audio device \"" + device_ + "\": " + snd_strerror(err));
    }
    // ⚠️ A suspended sink can swallow the first ~100 ms while it wakes up: lead with silence.
    const std::vector<int16_t> lead(static_cast<size_t>(sample_rate / 10), 0);
    write(lead);
}

void AlsaSink::write(std::span<const int16_t> samples) {
    while (pcm_ && !samples.empty()) {
        const snd_pcm_sframes_t n = snd_pcm_writei(pcm_, samples.data(), samples.size());
        if (n < 0) {
            // -EPIPE = underrun (synthesis of the next sentence took longer than the buffer): recoverable.
            if (snd_pcm_recover(pcm_, static_cast<int>(n), /*silent=*/1) == 0) continue;
            throw std::runtime_error(std::string("audio write failed: ") + snd_strerror(static_cast<int>(n)));
        }
        samples = samples.subspan(static_cast<size_t>(n));
    }
}

void AlsaSink::end() {
    if (!pcm_) return;
    snd_pcm_drain(pcm_);
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
}

// ---------------------------------------------------------------- WAV

namespace {

void put_le(std::ofstream& f, uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) f.put(static_cast<char>((v >> (8 * i)) & 0xff));
}

} // namespace

void WavSink::begin(int sample_rate) {
    sample_rate_ = sample_rate;
    samples_.clear();
}

void WavSink::write(std::span<const int16_t> samples) { samples_.insert(samples_.end(), samples.begin(), samples.end()); }

void WavSink::end() {
    std::ofstream f(path_, std::ios::binary);
    const auto data_bytes = static_cast<uint32_t>(samples_.size() * 2);
    f.write("RIFF", 4);
    put_le(f, 36 + data_bytes, 4);
    f.write("WAVEfmt ", 8);
    put_le(f, 16, 4);                                       // fmt chunk size
    put_le(f, 1, 2);                                        // PCM
    put_le(f, 1, 2);                                        // mono
    put_le(f, static_cast<uint32_t>(sample_rate_), 4);
    put_le(f, static_cast<uint32_t>(sample_rate_) * 2, 4);  // byte rate
    put_le(f, 2, 2);                                        // block align
    put_le(f, 16, 2);                                       // bits per sample
    f.write("data", 4);
    put_le(f, data_bytes, 4);
    for (const int16_t s : samples_) put_le(f, static_cast<uint16_t>(s), 2);
    if (!f) throw std::runtime_error("cannot write " + path_);
}

} // namespace buddy::voice
