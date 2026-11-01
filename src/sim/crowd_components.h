// Scene components of the crowd demo. Their reflection lives in crowd_components.cpp; a scene
// sets them in TOML (see examples/) and the app turns them into a sim::CrowdConfig.
#pragma once

#include "core/reflect.h"

#include <stdint.h>

namespace zaro::sim {

struct CrowdArena {
    uint32_t grid_size = 256;  // cells of 1 m per side
    uint64_t seed = 1;
};

struct CrowdSpawner {
    uint32_t count = 50000;
    float speed = 4.0f;
    float radius = 0.35f;
};

struct PlayerController {
    float speed = 8.0f;
};

extern const reflect::ComponentDesc kCrowdArenaDesc;
extern const reflect::ComponentDesc kCrowdSpawnerDesc;
extern const reflect::ComponentDesc kPlayerControllerDesc;

void register_crowd_components();

}  // namespace zaro::sim
