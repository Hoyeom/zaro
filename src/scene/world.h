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

// Position, turn and size relative to the parent, or to the world at the top level. A parent
// moves, turns and scales its children; turning by `rotation` degrees takes +x toward +z, like
// Shape.rotation and ParticleEmitter.direction. An
// entity without one sits at its parent's origin.
struct Transform {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation = 0.0f;
    float scale = 1.0f;
};

extern const reflect::ComponentDesc kTransformDesc;
void register_scene_components();

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

// Marks the entity and all its descendants destroyed. They are skipped by lookups at once and
// removed by flush_entities.
void destroy_entity(Scene* scene, Entity* entity);
// Removes the destroyed entities, keeping the order of the rest. Returns how many were removed.
uint32_t flush_entities(Scene* scene);

// Live (not destroyed) entities by id or by name; nullptr if there is none.
Entity* entity_by_id(Scene* scene, uint32_t id);
Entity* entity_by_name(Scene* scene, const char* name);
// The closest live entity name for "did you mean", or nullptr.
const char* suggest_entity(const Scene* scene, const char* name);

// Makes `parent` (nullptr: none) the entity's parent. False with a reason in err if that would
// make a loop. The child keeps its Transform, so it moves to the same place relative to the parent.
bool set_parent(Scene* scene, Entity* child, Entity* parent, char* err, uint32_t err_size);
// The live parent, or nullptr.
Entity* parent_of(Scene* scene, const Entity* entity);

// The entity's world transform now, from its Transform and its parents' (deterministic math).
WorldTransform world_transform(Scene* scene, const Entity* entity);
// Fills Entity::world for every live entity; the app calls it at the end of each tick.
void update_transforms(Scene* scene);
// A point in an entity's local space, in the world.
void transform_point(const WorldTransform& world, const float local[3], float out[3]);

// Hash of the gameplay state: every live entity's id, name, parent and the fields of its components
// that are not visual (reflect::is_visual). Visual data such as sprites and tweened values is left
// out, so how a scene looks never changes a golden hash.
uint64_t world_hash(const Scene* scene);

}  // namespace zaro::scene
