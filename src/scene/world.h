// A loaded scene's entities at run time (D-031): spawning from the scene's prefabs, destroying,
// lookup by id or name, and the state hash of what is gameplay.
//
// Entities keep the order of their ids. Spawning appends; destroying only marks the entity, and
// flush_entities removes the marked ones at the end of the tick, so ids, indexes and iteration
// stay valid while scripts run. Both bump Scene::generation, after which pointers into
// Scene::entities must be looked up again.
#pragma once

#include "scene/scene.h"

#include <stdint.h>

namespace zaro::scene {

// At most this many entities exist at once (spawn fails beyond it).
constexpr uint32_t kMaxLiveEntities = 65536;

// The index of a [scene] prefabs entry by its [prefab] name, or -1.
int find_spawn_prefab(const Scene* scene, const char* name);
// The closest prefab name for "did you mean", or nullptr.
const char* suggest_spawn_prefab(const Scene* scene, const char* name);

// Appends an entity with the prefab's components (prefab -1: none) and a new id. `name` may be
// nullptr, which names it "<prefab>#<id>" (or "entity#<id>"). Returns nullptr with a reason in err
// when the name is taken or there are kMaxLiveEntities entities.
Entity* spawn_entity(Scene* scene, int prefab, const char* name, char* err, uint32_t err_size);
// Adds a component with its default values to an entity that lacks it; returns its data. A spawned
// entity's components have no field lines.
void* add_component(Entity* entity, const reflect::ComponentDesc* desc);

// Marks the entity destroyed. It is skipped by lookups at once and removed by flush_entities.
void destroy_entity(Scene* scene, Entity* entity);
// Removes the destroyed entities, keeping the order of the rest. Returns how many were removed.
uint32_t flush_entities(Scene* scene);

// Live (not destroyed) entities by id or by name; nullptr if there is none.
Entity* entity_by_id(Scene* scene, uint32_t id);
Entity* entity_by_name(Scene* scene, const char* name);
// The closest live entity name for "did you mean", or nullptr.
const char* suggest_entity(const Scene* scene, const char* name);

// Hash of the gameplay state: every live entity's id and name and the fields of its components
// that are not visual (reflect::is_visual). Visual data such as sprites and tweened values is left
// out, so how a scene looks never changes a golden hash.
uint64_t world_hash(const Scene* scene);

}  // namespace zaro::scene
