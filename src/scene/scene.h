// Text scenes, screens and prefabs (TOML), loaded into registered components (core/reflect.h).
//
//   [scene]                          # required in a scene file
//   name = "crowd"
//   script = "crowd.luau"            # optional, relative to the scene file
//   ui = ["ui/hud.toml"]             # optional: root screens, always shown over the scene
//   screens = ["ui/settings.toml"]   # optional: screens that can be opened by name (D-021)
//   strings = "strings"              # optional: directory of string tables, <language>.toml
//   languages = ["ko", "en"]         # with strings: the languages it holds; the first is the default
//   build_extensions = ["ext.toml"]  # optional: what plugins add to an Android export (scene/build_extension.h)
//   prefabs = ["prefabs/enemy.toml"] # optional: prefabs scripts can spawn by their [prefab] name (scene/world.h)
//
//   [[entity]]
//   name = "player"                  # required, unique in the scene
//   prefab = "prefabs/player.toml"   # optional, relative to the scene file
//   [entity.PlayerController]        # a registered scene component; fields override the prefab
//   speed = 9.0
//
// A screen file (D-020) has the same shape with UI widgets on nodes instead of components on
// entities. A node's parent must come earlier in the file; nodes without one are top level.
//
//   [screen]                         # required in a screen file
//   name = "hud"
//   theme = "theme.toml"             # optional: "$name" values come from this theme (scene/theme.h)
//   reference = [1280, 720]          # optional design size; the screen scales to fit the target
//   navigable = false                # optional: arrow keys and d-pad move the focus on this screen
//   dismissible = true               # optional: back closes the screen when it was opened (D-021)
//   transition = "pop"               # optional: none, fade or pop (fade and grow), when it opens and closes
//   transition_time = 0.15           # optional, seconds
//   skippable = true                 # optional: back during the entrance jumps to its end
//
//   [[node]]
//   name = "gold"                    # required, unique in the screen
//   parent = "top_bar"               # optional
//   [node.Label]                     # a UI widget (`zaro schema` lists them with "kind": "ui")
//   text = "@hud.gold"               # text starting with @ is a key into the string tables
//
// A prefab file has a [prefab] table with a name and top-level component tables. Prefabs do not
// nest. Values start from the component defaults, then the prefab, then the entity. A prefab in
// [scene] prefabs needs its name, which scripts pass to zaro.spawn; names are unique in a scene.
#pragma once

#include "core/diag.h"
#include "core/reflect.h"

#include <stdint.h>

namespace zaro::scene {

constexpr uint32_t kNameCapacity = 64;
constexpr uint32_t kPathCapacity = 260;
constexpr uint32_t kMaxSceneScreens = 8;
constexpr uint32_t kMaxLanguages = 8;
constexpr uint32_t kLanguageCapacity = 16;
constexpr uint32_t kMaxBuildExtensions = 16;
constexpr uint32_t kMaxSpawnPrefabs = 64;

enum class DocKind : uint8_t { kScene, kScreen };

enum ScreenTransition : uint32_t { kTransitionNone, kTransitionFade, kTransitionPop };

struct ComponentData {
    const reflect::ComponentDesc* desc;
    void* data;        // desc->size bytes
    int line;          // line of the component's table, 0 when it came from a prefab only
    int* field_lines;  // per field: line where the document set it, 0 for defaults and prefab values
};

struct Entity {
    char name[kNameCapacity];
    char prefab[kPathCapacity];  // as written in the file, empty if none
    int line;                    // line of the entity or node in the file
    char parent[kNameCapacity];  // screens: the parent node's name, empty at the top level
    int parent_index;            // screens: index of the parent in `entities`, -1 at the top level
    ComponentData* components;
    uint32_t component_count;
    // Scenes: unique in the run and never reused. The file's entities are 1..n in file order and
    // spawned ones continue (scene/world.h), so `entities` is always in id order.
    uint32_t id;
    bool destroyed;  // destroyed this tick; removed by flush_entities at the end of it
};

// A prefab from [scene] prefabs, kept loaded so scripts can spawn it (scene/world.h).
struct SpawnPrefab {
    char name[kNameCapacity];
    char path[kPathCapacity];  // resolved
    ComponentData* components;
    uint32_t component_count;
};

struct Scene {
    char path[kPathCapacity];
    char name[kNameCapacity];
    char script[kPathCapacity];  // resolved path, empty if none
    DocKind kind;

    // Scenes only.
    char ui[kMaxSceneScreens][kPathCapacity];  // resolved root screen paths, bottom to top
    int ui_line;
    uint32_t ui_count;
    char screens[kMaxSceneScreens * 4][kPathCapacity];  // resolved paths of screens opened by name
    int screens_line;
    uint32_t screen_count;
    char strings[kPathCapacity];  // resolved directory of string tables with a trailing '/', or empty
    int strings_line;
    char languages[kMaxLanguages][kLanguageCapacity];
    uint32_t language_count;
    char build_extensions[kMaxBuildExtensions][kPathCapacity];  // resolved paths (build_extension.h)
    int build_extensions_line;
    uint32_t build_extension_count;
    SpawnPrefab* prefabs;
    uint32_t prefab_count;
    int prefabs_line;

    // Run time (scene/world.h).
    uint32_t entity_capacity;  // allocated length of `entities`
    uint32_t next_id;
    // Changes whenever entities are added or removed, which may move `entities`. Code that keeps
    // pointers into it rebuilds them when this differs from the value it saw.
    uint64_t generation;

    // Screens only.
    char theme[kPathCapacity];  // resolved theme path, or empty
    int theme_line;
    bool navigable;
    float reference[2];  // design size; 1280 x 720 when not given
    bool dismissible;    // true when not given
    uint32_t transition; // ScreenTransition
    float transition_time;
    bool skippable;      // true when not given

    Entity* entities;  // entities of a scene, nodes of a screen (parents before their children)
    uint32_t entity_count;
};

// Loads and validates a scene or a screen file. Every problem is added to diags; returns nullptr
// if there was any. load_scene and load_screen also reject the other kind of file.
Scene* load_document(const char* path, DiagList* diags);
Scene* load_scene(const char* path, DiagList* diags);
Scene* load_screen(const char* path, DiagList* diags);
void free_scene(Scene* scene);

const Entity* find_entity(const Scene* scene, const char* name);
const void* get_component(const Entity* entity, const reflect::ComponentDesc* desc);
const ComponentData* find_component_data(const Entity* entity, const reflect::ComponentDesc* desc);
// First entity (in file order) that has the component, or nullptr.
const void* first_component(const Scene* scene, const reflect::ComponentDesc* desc, const Entity** entity);
// Writable variant for the owner of the scene (the app and scripts change values at run time).
void* first_component_mut(Scene* scene, const reflect::ComponentDesc* desc);

}  // namespace zaro::scene
