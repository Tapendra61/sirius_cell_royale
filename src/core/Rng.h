#pragma once

#include <cstdint>

namespace cr {

// xorshift64 (Marsaglia 2003). Period 2^64 - 1. Seed must be non-zero.
struct Rng {
    uint64_t state;

    explicit Rng(uint64_t seed = 0xC0FFEEull) : state(seed ? seed : 0xC0FFEEull) {}

    uint64_t nextU64() {
        uint64_t x = state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        state = x;
        return x;
    }

    // Uniform float in [0, 1).
    float nextFloat() {
        return static_cast<float>(nextU64() >> 40) * (1.0f / static_cast<float>(1 << 24));
    }

    // Uniform float in [lo, hi).
    float rangeFloat(float lo, float hi) { return lo + nextFloat() * (hi - lo); }

    // Uniform int in [lo, hi].
    int rangeInt(int lo, int hi) {
        if (hi <= lo) return lo;
        uint64_t n = static_cast<uint64_t>(hi - lo + 1);
        return lo + static_cast<int>(nextU64() % n);
    }
};

} // namespace cr
