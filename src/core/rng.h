// Deterministic random numbers. The only randomness allowed in simulation code (D-005).
#pragma once

#include <stdint.h>

namespace zaro {

// PCG32 (O'Neill), small and fast with good statistics.
struct Rng {
    uint64_t state;
    uint64_t inc;
};

inline uint32_t rng_next(Rng& r) {
    const uint64_t old = r.state;
    r.state = old * 6364136223846793005ULL + r.inc;
    const uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    const uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

inline Rng rng_seed(uint64_t seed, uint64_t stream) {
    Rng r{0u, (stream << 1u) | 1u};
    rng_next(r);
    r.state += seed;
    rng_next(r);
    return r;
}

// Uniform float in [0, 1).
inline float rng_float(Rng& r) { return (float)(rng_next(r) >> 8) * (1.0f / 16777216.0f); }

// Stateless hash for order-independent per-entity randomness in parallel loops.
inline uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

}  // namespace zaro
