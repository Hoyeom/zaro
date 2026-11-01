// The crowd simulation must give the same state hash regardless of thread count (D-005, D-007).
#include "core/jobs.h"
#include "sim/crowd.h"

#include <stdio.h>

namespace {

uint64_t run(uint32_t threads, uint32_t ticks) {
    zaro::jobs::init(threads);
    zaro::sim::CrowdConfig cfg;
    cfg.enemy_count = 20000;
    cfg.seed = 7;
    zaro::sim::Crowd* crowd = zaro::sim::create_crowd(cfg);
    for (uint32_t t = 0; t < ticks; ++t) zaro::sim::step(crowd, zaro::sim::scripted_input(t));
    const uint64_t h = zaro::sim::state_hash(crowd);
    zaro::sim::destroy_crowd(crowd);
    zaro::jobs::shutdown();
    return h;
}

}  // namespace

int main() {
    const uint32_t ticks = 300;
    const uint64_t single = run(1, ticks);
    const uint64_t multi = run(8, ticks);
    const uint64_t again = run(8, ticks);
    printf("single=%016llx multi=%016llx again=%016llx\n", (unsigned long long)single,
           (unsigned long long)multi, (unsigned long long)again);
    if (single != multi || multi != again) {
        fprintf(stderr, "zaro: error: crowd simulation is not deterministic across thread counts\n");
        return 1;
    }
    return 0;
}
