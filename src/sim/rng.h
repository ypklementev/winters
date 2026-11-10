// Small, fast, deterministic PRNG (xoshiro128+ seeded via splitmix64).
#pragma once

#include <cstdint>

namespace sim {

class Rng {
public:
    explicit Rng(std::uint64_t seed = 0x5EEDF1A4Eull) { reseed(seed); }

    void reseed(std::uint64_t seed) {
        for (auto& w : s_) {
            seed += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z ^= z >> 31;
            w = static_cast<std::uint32_t>(z);
        }
        if ((s_[0] | s_[1] | s_[2] | s_[3]) == 0) s_[0] = 1;
    }

    std::uint32_t next() {
        const std::uint32_t result = s_[0] + s_[3];
        const std::uint32_t t = s_[1] << 9;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = (s_[3] << 11) | (s_[3] >> 21);
        return result;
    }

    // Uniform in [0, 1).
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }

    // Uniform in [lo, hi).
    float uniform(float lo, float hi) { return lo + (hi - lo) * uniform(); }

private:
    std::uint32_t s_[4]{};
};

}  // namespace sim
