#include "audio_source.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include <alsa/asoundlib.h>

#include "buddy/lifecycle.hpp"
#include "buddy/systemd.hpp"

namespace buddy::ears {

// ---------------------------------------------------------------- ALSA

AlsaSource::AlsaSource(std::string device, int max_queue_ms)
    : device_(std::move(device)),
      max_chunks_(std::max<std::size_t>(1, max_queue_ms * kSampleRate / 1000 / kChunkSamples)),
      thread_(&AlsaSource::capture_loop, this) {}

AlsaSource::~AlsaSource() {
    {
        std::lock_guard lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

bool AlsaSource::read(Chunk& chunk) {
    std::unique_lock lock(mutex_);
    // Timed wait: re-check running() so Ctrl+C works even if the microphone delivers nothing.
    while (queue_.empty()) {
        if (stopped_ || !running()) return false;
        cv_.wait_for(lock, std::chrono::milliseconds(200));
    }
    chunk = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

void AlsaSource::capture_loop() {
    using namespace std::chrono_literals;
    bool warned_open = false;

    while (running()) {
        snd_pcm_t* pcm = nullptr;
        int err = snd_pcm_open(&pcm, device_.c_str(), SND_PCM_STREAM_CAPTURE, 0);
        if (err == 0) {
            // soft_resample=1: PipeWire/ALSA converts from the Jabra's native rate to 16 kHz.
            err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                     /*channels=*/1, kSampleRate, /*soft_resample=*/1, /*latency_us=*/100000);
        }
        if (err < 0) {
            // At boot the Jabra / PipeWire may not be ready yet: keep retrying, but log only once.
            if (!warned_open) {
                std::cout << "[ears] cannot open audio device \"" << device_ << "\": " << snd_strerror(err)
                          << ", retrying every 2 s" << std::endl;
                warned_open = true;
            }
            if (pcm) snd_pcm_close(pcm);
            systemd::notify_watchdog();  // waiting for the device is healthy, not hung
            std::this_thread::sleep_for(2s);
            continue;
        }
        std::cout << "[ears] capturing from \"" << device_ << "\" at " << kSampleRate << " Hz" << std::endl;
        warned_open = false;

        Chunk chunk(kChunkSamples);
        while (running()) {
            const snd_pcm_sframes_t n = snd_pcm_readi(pcm, chunk.data(), kChunkSamples);
            if (n < 0) {
                // -EPIPE = overrun (we were too slow), -ESTRPIPE = suspend: recoverable.
                if (snd_pcm_recover(pcm, static_cast<int>(n), /*silent=*/1) == 0) continue;
                std::cout << "[ears] audio read failed: " << snd_strerror(static_cast<int>(n)) << ", reopening"
                          << std::endl;
                break;  // e.g. Jabra unplugged
            }
            if (static_cast<std::size_t>(n) != kChunkSamples) continue;  // only after a recover; drop it

            std::lock_guard lock(mutex_);
            if (stopped_) break;
            if (queue_.size() >= max_chunks_) queue_.pop_front();
            queue_.push_back(chunk);
            cv_.notify_one();
        }
        snd_pcm_close(pcm);

        std::lock_guard lock(mutex_);
        if (stopped_) return;
    }
    cv_.notify_all();
}

// ---------------------------------------------------------------- WAV

namespace {

uint32_t le32(const char* p) {
    return static_cast<uint8_t>(p[0]) | static_cast<uint8_t>(p[1]) << 8 | static_cast<uint8_t>(p[2]) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(p[3])) << 24;
}
uint16_t le16(const char* p) { return static_cast<uint16_t>(static_cast<uint8_t>(p[0]) | static_cast<uint8_t>(p[1]) << 8); }

} // namespace

WavSource::WavSource(const std::string& path) : file_(path, std::ios::binary) {
    const std::string fix = " (convert: ffmpeg -i in.wav -ar 16000 -ac 1 -c:a pcm_s16le out.wav)";
    if (!file_) throw std::runtime_error("cannot open " + path);

    char riff[12];
    if (!file_.read(riff, 12) || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0)
        throw std::runtime_error(path + " is not a WAV file");

    bool format_ok = false;
    char header[8];
    while (file_.read(header, 8)) {
        const uint32_t size = le32(header + 4);
        if (std::memcmp(header, "fmt ", 4) == 0) {
            std::vector<char> fmt(size);
            file_.read(fmt.data(), size);
            const uint16_t tag = le16(fmt.data());  // 1 = PCM, 0xFFFE = extensible (pw-record writes this)
            const uint16_t channels = le16(fmt.data() + 2);
            const uint32_t rate = le32(fmt.data() + 4);
            const uint16_t bits = le16(fmt.data() + 14);
            if ((tag != 1 && tag != 0xFFFE) || channels != 1 || rate != kSampleRate || bits != 16)
                throw std::runtime_error(path + ": need 16 kHz mono 16-bit PCM, got " + std::to_string(rate) + " Hz, " +
                                         std::to_string(channels) + " ch, " + std::to_string(bits) + " bit" + fix);
            format_ok = true;
        } else if (std::memcmp(header, "data", 4) == 0) {
            if (!format_ok) throw std::runtime_error(path + ": data before fmt chunk");
            remaining_ = size;  // may be 0 or bogus if the recorder was killed; read() stops at EOF anyway
            if (remaining_ == 0) remaining_ = SIZE_MAX;
            return;
        } else {
            file_.seekg(size + (size & 1), std::ios::cur);  // chunks are padded to even sizes
        }
    }
    throw std::runtime_error(path + ": no data chunk");
}

bool WavSource::read(Chunk& chunk) {
    chunk.assign(kChunkSamples, 0);
    if (remaining_ > 0 && file_) {
        const std::size_t want = std::min(remaining_, kChunkSamples * sizeof(int16_t));
        file_.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(want));
        const auto got = static_cast<std::size_t>(file_.gcount());
        remaining_ -= std::min(remaining_, got);
        if (got > 0) return true;  // a short last chunk is zero-padded
        remaining_ = 0;
    }
    return trailing_silence_chunks_-- > 0;
}

} // namespace buddy::ears
