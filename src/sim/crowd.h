// Crowd simulation: many enemies chase one player across an arena with obstacles.
//
// This is the model. It knows nothing about windows or GPUs, runs in a fixed 60 Hz tick and
// is deterministic: the same config and inputs give the same state hash for any thread count.
#pragma once

#include <stdint.h>

namespace zaro::sim {

constexpr float kTickSeconds = 1.0f / 60.0f;

struct CrowdConfig {
    uint32_t enemy_count = 50000;
    uint32_t enemy_capacity = 0;  // most enemies at once, for set_enemy_count (0: enemy_count)
    uint32_t grid_size = 256;  // arena is grid_size x grid_size cells of 1 m
    uint64_t seed = 1;
    float enemy_speed = 4.0f;
    float player_speed = 8.0f;
    float enemy_radius = 0.35f;
};

// Per-tick input. Recording these is enough to replay a run.
struct CrowdInput {
    float move_x = 0.0f;  // -1..1 along world +x
    float move_z = 0.0f;  // -1..1 along world +z
};

struct Crowd;

Crowd* create_crowd(const CrowdConfig& config);
void destroy_crowd(Crowd* crowd);
void step(Crowd* crowd, const CrowdInput& input);
// Changes speeds from the next tick on (scripts tune these while the game runs). Arena size, seed
// and the enemy capacity are fixed when the crowd is created.
void set_tuning(Crowd* crowd, float enemy_speed, float player_speed);
// Changes how many enemies there are, up to the capacity, between steps (D-034). New ones appear at
// the arena edge, standing, from a hash of their index and the tick; lowering the count retires the
// enemies with the highest indices, which leave without a hit or a kill. Returns the count now.
uint32_t set_enemy_count(Crowd* crowd, uint32_t count);

uint64_t tick(const Crowd* crowd);
uint32_t enemy_count(const Crowd* crowd);
uint32_t enemy_capacity(const Crowd* crowd);
// Enemies that reached the player and were respawned at the arena edge, since start.
uint64_t total_hits(const Crowd* crowd);
// Where enemies reached the player in the last step (before they were respawned), as x, z pairs in
// enemy order; returns how many. For effects only: it is not part of the state or its hash.
uint32_t last_hits(const Crowd* crowd, const float** xz);
// Sweeps away the enemies within `radius` of (x, z), at most `max` (0: all) in enemy order: they
// respawn at the arena edge as after a hit, but count as kills. For gameplay scripts between steps
// (D-031); deterministic. Returns how many.
uint32_t kill_enemies(Crowd* crowd, float x, float z, float radius, uint32_t max);
uint64_t total_kills(const Crowd* crowd);
uint64_t state_hash(const Crowd* crowd);

// Interpolated view of the state for rendering. alpha in [0, 1] blends the previous tick
// (0) into the current one (1).
void player_position(const Crowd* crowd, float alpha, float* x, float* z);
// Writes enemy_count * 4 floats: x, z, heading_x, heading_z.
void write_enemy_instances(const Crowd* crowd, float alpha, float* out);
// Obstacle cells as (x, z) cell coordinates, for drawing. Returns the count.
uint32_t obstacle_cells(const Crowd* crowd, const uint32_t** cells_xz_pairs);
float arena_size(const Crowd* crowd);

// The scripted player path used by headless runs and tests: a deterministic circle.
CrowdInput scripted_input(uint64_t tick);

}  // namespace zaro::sim
