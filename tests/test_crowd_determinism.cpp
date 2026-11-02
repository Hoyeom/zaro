// The crowd simulation must give the same state hash regardless of thread count (D-005, D-007).
#include "core/jobs.h"
#include "sim/crowd.h"

#include <stdio.h>

namespace {

// With `resize`, the crowd starts smaller than its capacity and grows and shrinks while it runs (D-034).
uint64_t run(uint32_t threads, uint32_t ticks, bool resize = false) {
    zaro::jobs::init(threads);
    zaro::sim::CrowdConfig cfg;
    cfg.kinds[0].count = resize ? 2000 : 20000;
    cfg.kinds[0].capacity = resize ? 20000 : 0;
    cfg.seed = 7;
    zaro::sim::Crowd* crowd = zaro::sim::create_crowd(cfg);
    for (uint32_t t = 0; t < ticks; ++t) {
        if (resize && t % 30 == 0) zaro::sim::set_enemy_count(crowd, 0, t < 200 ? 2000 + t * 90 : 9000);
        zaro::sim::step(crowd, zaro::sim::scripted_input(t));
    }
    const uint64_t h = zaro::sim::state_hash(crowd);
    zaro::sim::destroy_crowd(crowd);
    zaro::jobs::shutdown();
    return h;
}

// Three kinds of enemy with their own speed, size and capacity (D-036): the first and last grow and
// shrink while a sweep that takes only the middle kind runs, so later kinds move in memory.
uint64_t run_kinds(uint32_t threads, uint32_t ticks, uint32_t* swept) {
    zaro::jobs::init(threads);
    zaro::sim::CrowdConfig cfg;
    cfg.kind_count = 3;
    cfg.kinds[0] = {3000, 9000, 4.0f, 0.35f};
    cfg.kinds[1] = {1000, 0, 7.0f, 0.25f};
    cfg.kinds[2] = {200, 2000, 2.5f, 0.8f};
    cfg.seed = 11;
    zaro::sim::Crowd* crowd = zaro::sim::create_crowd(cfg);
    *swept = 0;
    for (uint32_t t = 0; t < ticks; ++t) {
        if (t % 40 == 0) zaro::sim::set_enemy_count(crowd, 0, 3000 + t * 20);
        if (t % 50 == 25) zaro::sim::set_enemy_count(crowd, 2, t < 150 ? 200 + t * 8 : 100);
        float px, pz;
        zaro::sim::player_position(crowd, 1.0f, &px, &pz);
        uint32_t per_kind[zaro::sim::kMaxCrowdKinds];
        *swept += zaro::sim::kill_enemies(crowd, px + 2.0f, pz, 1.5f, 0, 1, per_kind);
        if (per_kind[0] || per_kind[2]) *swept += 1000000;  // only the middle kind may be swept
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
    uint32_t swept_single = 0, swept_multi = 0;
    const uint64_t kinds_single = run_kinds(1, ticks, &swept_single);
    const uint64_t kinds_multi = run_kinds(8, ticks, &swept_multi);
    printf("kinds single=%016llx multi=%016llx swept=%u\n", (unsigned long long)kinds_single,
           (unsigned long long)kinds_multi, swept_single);
    if (kinds_single != kinds_multi || swept_single != swept_multi) {
        fprintf(stderr, "zaro: error: a crowd with several kinds is not deterministic across thread counts\n");
        return 1;
    }
    if (swept_single >= 1000000) {
        fprintf(stderr, "zaro: error: kill_enemies with a kind swept other kinds\n");
        return 1;
    }
    zaro::jobs::init(1);
    zaro::sim::CrowdConfig cfg;
    cfg.kind_count = 2;
    cfg.kinds[0].count = 10;
    cfg.kinds[0].capacity = 100;
    cfg.kinds[1].count = 5;
    zaro::sim::Crowd* crowd = zaro::sim::create_crowd(cfg);
    const bool clamped = zaro::sim::set_enemy_count(crowd, 0, 500) == 100 && zaro::sim::enemy_count(crowd) == 105;
    uint32_t first = 0;
    const bool packed = zaro::sim::kind_enemies(crowd, 1, &first) == 5 && first == 100;
    const bool lowered = zaro::sim::set_enemy_count(crowd, 0, 3) == 3 && zaro::sim::enemy_capacity(crowd) == 105 &&
                         zaro::sim::kind_enemies(crowd, 1, &first) == 5 && first == 3;
    const bool fixed = zaro::sim::set_enemy_count(crowd, 1, 6) == 5 && zaro::sim::kind_capacity(crowd, 1) == 5;
    zaro::sim::destroy_crowd(crowd);
    zaro::jobs::shutdown();
    if (!packed || !fixed) {
        fprintf(stderr, "zaro: error: kinds are not kept packed in order, or a kind grew past its capacity\n");
        return 1;
    }
    if (!clamped || !lowered) {
        fprintf(stderr, "zaro: error: set_enemy_count does not keep the count within the capacity\n");
        return 1;
    }
    return 0;
}
