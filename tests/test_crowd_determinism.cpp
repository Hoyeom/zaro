// The crowd simulation must give the same state hash regardless of thread count (D-005, D-007).
#include "core/jobs.h"
#include "sim/crowd.h"

#include <stdio.h>

namespace {

// With `resize`, the crowd starts smaller than its capacity and grows and shrinks while it runs (D-034).
uint64_t run(uint32_t threads, uint32_t ticks, bool resize = false) {
    zaro::jobs::init(threads);
    zaro::sim::CrowdConfig cfg;
    cfg.enemy_count = resize ? 2000 : 20000;
    cfg.enemy_capacity = resize ? 20000 : 0;
    cfg.seed = 7;
    zaro::sim::Crowd* crowd = zaro::sim::create_crowd(cfg);
    for (uint32_t t = 0; t < ticks; ++t) {
        if (resize && t % 30 == 0) zaro::sim::set_enemy_count(crowd, t < 200 ? 2000 + t * 90 : 9000);
        zaro::sim::step(crowd, zaro::sim::scripted_input(t));
    }
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
    const uint64_t resized_single = run(1, ticks, true);
    const uint64_t resized_multi = run(8, ticks, true);
    printf("resized single=%016llx multi=%016llx\n", (unsigned long long)resized_single,
           (unsigned long long)resized_multi);
    if (resized_single != resized_multi) {
        fprintf(stderr, "zaro: error: a crowd that changes its count is not deterministic across thread counts\n");
        return 1;
    }
    zaro::jobs::init(1);
    zaro::sim::CrowdConfig cfg;
    cfg.enemy_count = 10;
    cfg.enemy_capacity = 100;
    zaro::sim::Crowd* crowd = zaro::sim::create_crowd(cfg);
    const bool clamped = zaro::sim::set_enemy_count(crowd, 500) == 100 && zaro::sim::enemy_count(crowd) == 100;
    const bool lowered = zaro::sim::set_enemy_count(crowd, 3) == 3 && zaro::sim::enemy_capacity(crowd) == 100;
    zaro::sim::destroy_crowd(crowd);
    zaro::jobs::shutdown();
    if (!clamped || !lowered) {
        fprintf(stderr, "zaro: error: set_enemy_count does not keep the count within the capacity\n");
        return 1;
    }
    return 0;
}
