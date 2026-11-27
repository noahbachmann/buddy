#pragma once
// Deterministic RNG that lives in the game state: seed + action log replay a game exactly. Own algorithms on purpose:
// std::mt19937 is 2.5 KB to copy with every state, and std's distributions and std::shuffle may differ between
// standard libraries (a game recorded on the Pi must replay on the PC).

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace buddy::riftbound {

// xoshiro256**, seeded through splitmix64 (Blackman & Vigna's reference algorithms).
class Rng {
public:
    explicit Rng(std::uint64_t seed = 0) {
        for (auto& word : s_) {
            seed += 0x9e3779b97f4a7c15;
            std::uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
            z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
            word = z ^ (z >> 31);
        }
    }

    std::uint64_t next() {
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // Uniform in [0, n), n > 0. Rejection sampling: no modulo bias.
    std::uint64_t below(std::uint64_t n) {
        const std::uint64_t limit = UINT64_MAX - UINT64_MAX % n;
        std::uint64_t x = next();
        while (x >= limit) x = next();
        return x % n;
    }

    // Fisher–Yates.
    template <class T>
    void shuffle(std::vector<T>& v) {
        for (std::size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[below(i)]);
    }

    bool operator==(const Rng&) const = default;

private:
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    std::array<std::uint64_t, 4> s_{};
};

} // namespace buddy::riftbound
