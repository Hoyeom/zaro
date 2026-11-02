#include "sim/crowd_components.h"

namespace zaro::sim {

namespace {

const reflect::FieldDesc kArenaFields[] = {
    ZARO_FIELD_RANGE(CrowdArena, grid_size, kU32, "Arena width and depth in 1 m cells", 32, 4096),
    ZARO_FIELD(CrowdArena, seed, kU64, "Seed for obstacles and spawn positions; same seed, same run"),
};

const reflect::FieldDesc kSpawnerFields[] = {
    ZARO_FIELD_RANGE(CrowdSpawner, count, kU32,
                     "Number of enemies; a script may change it while the game runs, up to max: new ones "
                     "appear at the arena edge, the highest-numbered ones leave",
                     0, 1000000),
    ZARO_FIELD_RANGE(CrowdSpawner, max, kU32,
                     "Most enemies at once, reserved at start (0: count at start); count is kept at or below it",
                     0, 1000000),
    ZARO_FIELD_RANGE(CrowdSpawner, speed, kF32, "Enemy speed in m/s", 0.0, 100.0),
    ZARO_FIELD_RANGE(CrowdSpawner, radius, kF32, "Enemy collision radius in m", 0.05, 5.0),
};

const reflect::FieldDesc kPlayerFields[] = {
    ZARO_FIELD_RANGE(PlayerController, speed, kF32, "Player speed in m/s", 0.0, 100.0),
};

}  // namespace

ZARO_COMPONENT(CrowdArena, "The arena: size and the seed for obstacles and spawning", kArenaFields);
ZARO_COMPONENT(CrowdSpawner,
               "Spawns enemies that chase the player along a flow field; each entity of the scene file with one "
               "is a kind of enemy (at most 8)",
               kSpawnerFields);
ZARO_COMPONENT(PlayerController, "The player the crowd chases", kPlayerFields);

void register_crowd_components() {
    reflect::register_component(&kCrowdArenaDesc);
    reflect::register_component(&kCrowdSpawnerDesc);
    reflect::register_component(&kPlayerControllerDesc);
}

}  // namespace zaro::sim
