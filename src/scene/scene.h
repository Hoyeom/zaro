// Text scenes and prefabs (TOML), loaded into registered components (core/reflect.h).
//
//   [scene]                          # required
//   name = "crowd"
//   script = "crowd.luau"            # optional, relative to the scene file
//
//   [[entity]]
//   name = "player"                  # required, unique in the scene
//   prefab = "prefabs/player.toml"   # optional, relative to the scene file
//   [entity.PlayerController]        # a registered component; fields override the prefab
//   speed = 9.0
//
// A prefab file has a [prefab] table with a name and top-level component tables. Prefabs do not
// nest. Values start from the component defaults, then the prefab, then the entity.
#pragma once

#include "core/diag.h"
#include "core/reflect.h"

#include <stdint.h>

namespace zaro::scene {

constexpr uint32_t kNameCapacity = 64;
constexpr uint32_t kPathCapacity = 260;

struct ComponentData {
    const reflect::ComponentDesc* desc;
    void* data;  // desc->size bytes
};

struct Entity {
    char name[kNameCapacity];
    char prefab[kPathCapacity];  // as written in the scene, empty if none
    int line;                    // line of the entity in the scene file
    ComponentData* components;
    uint32_t component_count;
};

struct Scene {
    char path[kPathCapacity];
    char name[kNameCapacity];
    char script[kPathCapacity];  // resolved path, empty if none
    Entity* entities;
    uint32_t entity_count;
};

// Loads and validates a scene. Every problem is added to diags; returns nullptr if there was any.
Scene* load_scene(const char* path, DiagList* diags);
void free_scene(Scene* scene);

const Entity* find_entity(const Scene* scene, const char* name);
const void* get_component(const Entity* entity, const reflect::ComponentDesc* desc);
// First entity (in file order) that has the component, or nullptr.
const void* first_component(const Scene* scene, const reflect::ComponentDesc* desc, const Entity** entity);
// Writable variant for the owner of the scene (the app and scripts change values at run time).
void* first_component_mut(Scene* scene, const reflect::ComponentDesc* desc);

}  // namespace zaro::scene
