#include "sim/crowd.h"

#include "core/hash.h"
#include "core/jobs.h"
#include "core/profile.h"
#include "core/rng.h"

#include <atomic>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::sim {

namespace {
constexpr float kPlayerRadius = 0.6f;
constexpr float kNearPlayer = 3.0f;        // inside this, steer straight at the player
constexpr float kSeparation = 8.0f;        // push strength between overlapping enemies
constexpr float kSteerBlend = 0.25f;       // velocity smoothing per tick
constexpr uint32_t kMaxNeighbors = 24;     // separation checks per enemy (keeps cost bounded)
constexpr uint16_t kUnreached = 0xFFFF;

template <typename T>
T* alloc(size_t n) {
    return (T*)calloc(n, sizeof(T));
}
}  // namespace

struct Crowd {
    CrowdConfig cfg;
    uint32_t g = 0;  // grid size
    uint64_t tick = 0;
    std::atomic<uint64_t> hits{0};

    // Arena.
    uint8_t* blocked = nullptr;
    uint32_t* obstacles = nullptr;  // (x, z) pairs
    uint32_t obstacle_count = 0;

    // Flow field toward the player.
    uint16_t* dist = nullptr;
    float* flow_x = nullptr;
    float* flow_z = nullptr;
    uint32_t* bfs_queue = nullptr;
    int64_t flow_cell = -1;

    // Player.
    float px = 0, pz = 0, prev_px = 0, prev_pz = 0;

    // Enemies (SoA).
    uint32_t n = 0;
    float *x = nullptr, *z = nullptr, *vx = nullptr, *vz = nullptr;
    float *prev_x = nullptr, *prev_z = nullptr, *nvx = nullptr, *nvz = nullptr;

    // Spatial hash: enemies sorted by grid cell (counting sort, stable).
    uint32_t* cell_of = nullptr;
    uint32_t* cell_start = nullptr;  // g*g + 1
    uint32_t* sorted = nullptr;
};

namespace {

inline uint32_t cell_index(const Crowd* c, float x, float z) {
    int cx = (int)x, cz = (int)z;
    cx = cx < 0 ? 0 : (cx >= (int)c->g ? (int)c->g - 1 : cx);
    cz = cz < 0 ? 0 : (cz >= (int)c->g ? (int)c->g - 1 : cz);
    return (uint32_t)cz * c->g + (uint32_t)cx;
}

inline bool is_blocked(const Crowd* c, float x, float z) {
    if (x < 0.0f || z < 0.0f || x >= (float)c->g || z >= (float)c->g) return true;
    return c->blocked[cell_index(c, x, z)] != 0;
}

void build_arena(Crowd* c, Rng& rng) {
    const uint32_t g = c->g;
    const uint32_t center = g / 2;
    const uint32_t rects = g / 4;
    for (uint32_t r = 0; r < rects; ++r) {
        const uint32_t w = 2 + rng_next(rng) % 7, h = 2 + rng_next(rng) % 7;
        const uint32_t x0 = 2 + rng_next(rng) % (g - w - 4), z0 = 2 + rng_next(rng) % (g - h - 4);
        // Keep the player's start area open.
        if (x0 + w > center - 12 && x0 < center + 12 && z0 + h > center - 12 && z0 < center + 12) continue;
        for (uint32_t zz = z0; zz < z0 + h; ++zz) {
            for (uint32_t xx = x0; xx < x0 + w; ++xx) c->blocked[zz * g + xx] = 1;
        }
    }
    for (uint32_t i = 0; i < g * g; ++i) c->obstacle_count += c->blocked[i];
    c->obstacles = alloc<uint32_t>((size_t)c->obstacle_count * 2 + 2);
    uint32_t k = 0;
    for (uint32_t i = 0; i < g * g; ++i) {
        if (!c->blocked[i]) continue;
        c->obstacles[k++] = i % g;
        c->obstacles[k++] = i / g;
    }
}

struct FlowCtx {
    Crowd* c;
};

void flow_directions(uint32_t begin, uint32_t end, void* ctx) {
    Crowd* c = ((FlowCtx*)ctx)->c;
    const int g = (int)c->g;
    static const int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int kDz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    for (uint32_t row = begin; row < end; ++row) {
        for (int cx = 0; cx < g; ++cx) {
            const uint32_t i = row * (uint32_t)g + (uint32_t)cx;
            c->flow_x[i] = 0.0f;
            c->flow_z[i] = 0.0f;
            if (c->blocked[i] || c->dist[i] == kUnreached) continue;
            uint16_t best = c->dist[i];
            int bx = 0, bz = 0;
            for (int k = 0; k < 8; ++k) {
                const int nx = cx + kDx[k], nz = (int)row + kDz[k];
                if (nx < 0 || nz < 0 || nx >= g || nz >= g) continue;
                const uint32_t j = (uint32_t)nz * (uint32_t)g + (uint32_t)nx;
                if (c->blocked[j]) continue;
                if (k >= 4) {  // diagonal: never cut a blocked corner
                    if (c->blocked[row * (uint32_t)g + (uint32_t)nx] || c->blocked[(uint32_t)nz * (uint32_t)g + (uint32_t)cx]) continue;
                }
                if (c->dist[j] < best) {
                    best = c->dist[j];
                    bx = kDx[k];
                    bz = kDz[k];
                }
            }
            const float len = sqrtf((float)(bx * bx + bz * bz));
            if (len > 0.0f) {
                c->flow_x[i] = (float)bx / len;
                c->flow_z[i] = (float)bz / len;
            }
        }
    }
}

void rebuild_flow(Crowd* c, uint32_t target) {
    ZARO_ZONE("sim.flow_field");
    const uint32_t g = c->g;
    for (uint32_t i = 0; i < g * g; ++i) c->dist[i] = kUnreached;
    uint32_t head = 0, tail = 0;
    c->dist[target] = 0;
    c->bfs_queue[tail++] = target;
    while (head < tail) {
        const uint32_t i = c->bfs_queue[head++];
        const uint32_t cx = i % g, cz = i / g;
        const uint16_t nd = (uint16_t)(c->dist[i] + 1);
        const uint32_t nbr[4] = {cx + 1 < g ? i + 1 : i, cx > 0 ? i - 1 : i, cz + 1 < g ? i + g : i, cz > 0 ? i - g : i};
        for (uint32_t j : nbr) {
            if (j == i || c->blocked[j] || c->dist[j] != kUnreached) continue;
            c->dist[j] = nd;
            c->bfs_queue[tail++] = j;
        }
    }
    FlowCtx ctx{c};
    jobs::parallel_for(g, 8, flow_directions, &ctx);
    c->flow_cell = target;
}

void build_spatial_hash(Crowd* c) {
    ZARO_ZONE("sim.spatial_hash");
    const uint32_t cells = c->g * c->g;
    memset(c->cell_start, 0, sizeof(uint32_t) * (cells + 1));
    for (uint32_t i = 0; i < c->n; ++i) {
        const uint32_t ci = cell_index(c, c->x[i], c->z[i]);
        c->cell_of[i] = ci;
        c->cell_start[ci + 1]++;
    }
    for (uint32_t i = 0; i < cells; ++i) c->cell_start[i + 1] += c->cell_start[i];
    // Per-cell write cursors; bfs_queue (g*g entries) is free between flow rebuilds.
    uint32_t* cursor = c->bfs_queue;
    memcpy(cursor, c->cell_start, sizeof(uint32_t) * cells);
    for (uint32_t i = 0; i < c->n; ++i) c->sorted[cursor[c->cell_of[i]]++] = i;
}

void steer(uint32_t begin, uint32_t end, void* ctx) {
    const Crowd* c = (const Crowd*)ctx;
    const int g = (int)c->g;
    const float speed = c->cfg.enemy_speed;
    const float diam = c->cfg.enemy_radius * 2.0f;
    for (uint32_t i = begin; i < end; ++i) {
        const float x = c->x[i], z = c->z[i];
        const float dx = c->px - x, dz = c->pz - z;
        const float d2 = dx * dx + dz * dz;
        float dir_x, dir_z;
        if (d2 < kNearPlayer * kNearPlayer) {
            const float inv = d2 > 1e-8f ? 1.0f / sqrtf(d2) : 0.0f;
            dir_x = dx * inv;
            dir_z = dz * inv;
        } else {
            const uint32_t ci = c->cell_of[i];
            dir_x = c->flow_x[ci];
            dir_z = c->flow_z[ci];
        }
        float push_x = 0.0f, push_z = 0.0f;
        uint32_t checked = 0;
        const int cx = (int)(c->cell_of[i] % c->g), cz = (int)(c->cell_of[i] / c->g);
        for (int oz = -1; oz <= 1 && checked < kMaxNeighbors; ++oz) {
            const int nz = cz + oz;
            if (nz < 0 || nz >= g) continue;
            for (int ox = -1; ox <= 1 && checked < kMaxNeighbors; ++ox) {
                const int nx = cx + ox;
                if (nx < 0 || nx >= g) continue;
                const uint32_t cell = (uint32_t)nz * (uint32_t)g + (uint32_t)nx;
                for (uint32_t s = c->cell_start[cell]; s < c->cell_start[cell + 1] && checked < kMaxNeighbors; ++s) {
                    const uint32_t j = c->sorted[s];
                    if (j == i) continue;
                    ++checked;
                    const float ex = x - c->x[j], ez = z - c->z[j];
                    const float e2 = ex * ex + ez * ez;
                    if (e2 >= diam * diam || e2 < 1e-10f) continue;
                    const float len = sqrtf(e2);
                    const float k = (diam - len) / (diam * len);
                    push_x += ex * k;
                    push_z += ez * k;
                }
            }
        }
        const float want_x = dir_x * speed + push_x * kSeparation;
        const float want_z = dir_z * speed + push_z * kSeparation;
        float nvx = c->vx[i] + (want_x - c->vx[i]) * kSteerBlend;
        float nvz = c->vz[i] + (want_z - c->vz[i]) * kSteerBlend;
        const float v2 = nvx * nvx + nvz * nvz, vmax = speed * 1.5f;
        if (v2 > vmax * vmax) {
            const float s = vmax / sqrtf(v2);
            nvx *= s;
            nvz *= s;
        }
        c->nvx[i] = nvx;
        c->nvz[i] = nvz;
    }
}

// Picks a free spot on the arena border from a per-enemy hash (order independent).
void respawn_position(const Crowd* c, uint32_t i, float* ox, float* oz) {
    const float size = (float)c->g;
    for (uint32_t attempt = 0; attempt < 8; ++attempt) {
        const uint32_t h = hash_u32(i * 2654435761u ^ (uint32_t)c->tick * 40503u ^ attempt * 97u ^ (uint32_t)c->cfg.seed);
        const float t = (float)(h >> 8) * (1.0f / 16777216.0f) * (size - 2.0f) + 1.0f;
        const float edge = 1.5f;
        float x, z;
        switch (h & 3u) {
            case 0: x = t; z = edge; break;
            case 1: x = t; z = size - edge; break;
            case 2: x = edge; z = t; break;
            default: x = size - edge; z = t; break;
        }
        if (!is_blocked(c, x, z)) {
            *ox = x;
            *oz = z;
            return;
        }
    }
    *ox = c->x[i];
    *oz = c->z[i];
}

struct IntegrateCtx {
    Crowd* c;
};

void integrate(uint32_t begin, uint32_t end, void* ctx) {
    Crowd* c = ((IntegrateCtx*)ctx)->c;
    const float hit_r = c->cfg.enemy_radius + kPlayerRadius;
    uint64_t hits = 0;
    for (uint32_t i = begin; i < end; ++i) {
        float x = c->x[i], z = c->z[i];
        float vx = c->nvx[i], vz = c->nvz[i];
        c->prev_x[i] = x;
        c->prev_z[i] = z;
        const float nx = x + vx * kTickSeconds;
        if (is_blocked(c, nx, z)) vx = 0.0f; else x = nx;
        const float nz = z + vz * kTickSeconds;
        if (is_blocked(c, x, nz)) vz = 0.0f; else z = nz;
        const float dx = c->px - x, dz = c->pz - z;
        if (dx * dx + dz * dz < hit_r * hit_r) {
            respawn_position(c, i, &x, &z);
            c->prev_x[i] = x;
            c->prev_z[i] = z;
            vx = vz = 0.0f;
            ++hits;
        }
        c->x[i] = x;
        c->z[i] = z;
        c->vx[i] = vx;
        c->vz[i] = vz;
    }
    if (hits) c->hits.fetch_add(hits, std::memory_order_relaxed);
}

struct InstanceCtx {
    const Crowd* c;
    float alpha;
    float* out;
};

void instances(uint32_t begin, uint32_t end, void* ctx) {
    const InstanceCtx* k = (const InstanceCtx*)ctx;
    const Crowd* c = k->c;
    for (uint32_t i = begin; i < end; ++i) {
        float* o = k->out + (size_t)i * 4;
        o[0] = c->prev_x[i] + (c->x[i] - c->prev_x[i]) * k->alpha;
        o[1] = c->prev_z[i] + (c->z[i] - c->prev_z[i]) * k->alpha;
        const float v2 = c->vx[i] * c->vx[i] + c->vz[i] * c->vz[i];
        if (v2 > 1e-6f) {
            const float inv = 1.0f / sqrtf(v2);
            o[2] = c->vx[i] * inv;
            o[3] = c->vz[i] * inv;
        } else {
            o[2] = 0.0f;
            o[3] = 1.0f;
        }
    }
}

}  // namespace

Crowd* create_crowd(const CrowdConfig& cfg) {
    Crowd* c = new Crowd();
    c->cfg = cfg;
    c->g = cfg.grid_size < 32 ? 32 : cfg.grid_size;
    const size_t cells = (size_t)c->g * c->g;
    c->blocked = alloc<uint8_t>(cells);
    c->dist = alloc<uint16_t>(cells);
    c->flow_x = alloc<float>(cells);
    c->flow_z = alloc<float>(cells);
    c->bfs_queue = alloc<uint32_t>(cells);
    c->cell_start = alloc<uint32_t>(cells + 1);

    c->n = cfg.enemy_count;
    const size_t n = c->n ? c->n : 1;
    c->x = alloc<float>(n);
    c->z = alloc<float>(n);
    c->vx = alloc<float>(n);
    c->vz = alloc<float>(n);
    c->prev_x = alloc<float>(n);
    c->prev_z = alloc<float>(n);
    c->nvx = alloc<float>(n);
    c->nvz = alloc<float>(n);
    c->cell_of = alloc<uint32_t>(n);
    c->sorted = alloc<uint32_t>(n);

    Rng rng = rng_seed(cfg.seed, 1);
    build_arena(c, rng);
    c->px = c->prev_px = (float)c->g * 0.5f;
    c->pz = c->prev_pz = (float)c->g * 0.5f;
    const float size = (float)c->g;
    for (uint32_t i = 0; i < c->n; ++i) {
        float x, z;
        do {
            x = 1.0f + rng_float(rng) * (size - 2.0f);
            z = 1.0f + rng_float(rng) * (size - 2.0f);
        } while (is_blocked(c, x, z) || ((x - c->px) * (x - c->px) + (z - c->pz) * (z - c->pz)) < 400.0f);
        c->x[i] = c->prev_x[i] = x;
        c->z[i] = c->prev_z[i] = z;
    }
    return c;
}

void destroy_crowd(Crowd* c) {
    if (!c) return;
    void* arrays[] = {c->blocked, c->obstacles, c->dist, c->flow_x, c->flow_z, c->bfs_queue, c->cell_start,
                      c->x, c->z, c->vx, c->vz, c->prev_x, c->prev_z, c->nvx, c->nvz, c->cell_of, c->sorted};
    for (void* p : arrays) free(p);
    delete c;
}

void step(Crowd* c, const CrowdInput& input) {
    ZARO_ZONE("sim.step");
    // Player.
    c->prev_px = c->px;
    c->prev_pz = c->pz;
    float mx = input.move_x, mz = input.move_z;
    const float m2 = mx * mx + mz * mz;
    if (m2 > 1.0f) {
        const float inv = 1.0f / sqrtf(m2);
        mx *= inv;
        mz *= inv;
    }
    const float step_len = c->cfg.player_speed * kTickSeconds;
    const float nx = c->px + mx * step_len, nz = c->pz + mz * step_len;
    if (!is_blocked(c, nx, c->pz)) c->px = nx;
    if (!is_blocked(c, c->px, nz)) c->pz = nz;

    const uint32_t pcell = cell_index(c, c->px, c->pz);
    if ((int64_t)pcell != c->flow_cell) rebuild_flow(c, pcell);

    build_spatial_hash(c);
    {
        ZARO_ZONE("sim.steer");
        jobs::parallel_for(c->n, 1024, steer, c);
    }
    {
        ZARO_ZONE("sim.integrate");
        IntegrateCtx ctx{c};
        jobs::parallel_for(c->n, 2048, integrate, &ctx);
    }
    c->tick++;
}

uint64_t tick(const Crowd* c) { return c->tick; }
uint32_t enemy_count(const Crowd* c) { return c->n; }
uint64_t total_hits(const Crowd* c) { return c->hits.load(std::memory_order_relaxed); }

uint64_t state_hash(const Crowd* c) {
    uint64_t h = fnv1a(&c->tick, sizeof(c->tick));
    const uint64_t hits = total_hits(c);
    h = fnv1a(&hits, sizeof(hits), h);
    h = fnv1a(&c->px, sizeof(float), h);
    h = fnv1a(&c->pz, sizeof(float), h);
    const size_t bytes = sizeof(float) * c->n;
    h = fnv1a(c->x, bytes, h);
    h = fnv1a(c->z, bytes, h);
    h = fnv1a(c->vx, bytes, h);
    h = fnv1a(c->vz, bytes, h);
    return h;
}

void player_position(const Crowd* c, float alpha, float* x, float* z) {
    *x = c->prev_px + (c->px - c->prev_px) * alpha;
    *z = c->prev_pz + (c->pz - c->prev_pz) * alpha;
}

void write_enemy_instances(const Crowd* c, float alpha, float* out) {
    ZARO_ZONE("sim.write_instances");
    InstanceCtx ctx{c, alpha, out};
    jobs::parallel_for(c->n, 4096, instances, &ctx);
}

uint32_t obstacle_cells(const Crowd* c, const uint32_t** cells) {
    *cells = c->obstacles;
    return c->obstacle_count;
}

float arena_size(const Crowd* c) { return (float)c->g; }

CrowdInput scripted_input(uint64_t t) {
    const float seconds = (float)t * kTickSeconds;
    const float angle = seconds * 0.25f;
    return CrowdInput{cosf(angle), sinf(angle)};
}

}  // namespace zaro::sim
