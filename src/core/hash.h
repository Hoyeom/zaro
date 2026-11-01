// State hashing for determinism checks and replays.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace zaro {

constexpr uint64_t kFnvOffset = 14695981039346656037ULL;

// FNV-1a over raw bytes. Chain calls by passing the previous result as `h`.
inline uint64_t fnv1a(const void* data, size_t size, uint64_t h = kFnvOffset) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

}  // namespace zaro
