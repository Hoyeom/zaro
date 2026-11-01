// Data-parallel loops on top of enkiTS (D-007).
//
// Determinism rule: a parallel_for body may write only to elements inside [begin, end) and
// may read anything that no iteration writes. Then results do not depend on thread count.
#pragma once

#include <stdint.h>

namespace zaro::jobs {

using RangeFn = void (*)(uint32_t begin, uint32_t end, void* ctx);

// threads == 0 picks the hardware thread count. threads == 1 runs everything inline.
void init(uint32_t threads);
void shutdown();
uint32_t thread_count();

// Splits [0, count) into ranges of at least min_range and runs fn on them; returns when done.
void parallel_for(uint32_t count, uint32_t min_range, RangeFn fn, void* ctx);

}  // namespace zaro::jobs
