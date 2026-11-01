// Sandboxed Luau game scripts (D-009).
//
// A script is a .luau file that may define two hooks:
//   function on_start()          -- once, after the scene is loaded
//   function on_tick(tick)       -- every simulation tick (60 Hz)
// and reaches the scene through one read-only global table:
//   zaro.get(entity, component, field)         -> number | boolean | string | {x, y[, z[, w]]}
//   zaro.set(entity, component, field, value)  -- type- and range-checked through reflection
//   zaro.has(entity[, component])              -> boolean
//   zaro.tick()                                -> current tick
//   zaro.log(...)                              -- prints "script: ..." to stderr
// and the scene's UI through `ui` (scene/ui_state.h):
//   ui.set(key, value)       -- a view-model value (number or string); texts show it as {key}
//   ui.get(key)              -> the value, or nil
//   ui.get_field(screen, node, component, field)         -- like zaro.get on a screen node
//   ui.set_field(screen, node, component, field, value)  -- like zaro.set, e.g. Layout.visible
//   ui.language(code)        -- switch to one of the scene's languages after this call
//   function on_ui(action, screen, node)   -- hook: a button with that action was clicked
// There is no os, io, loadstring or require, globals of the standard library are read-only, and
// math.random is seeded from the scene so runs stay deterministic.
#pragma once

#include "core/diag.h"
#include "scene/scene.h"
#include "scene/ui_state.h"

#include <stdint.h>

namespace zaro::script {

struct Script;

struct ScriptDesc {
    const char* path;
    scene::Scene* scene;          // the scene zaro.get / zaro.set act on
    scene::UiState* ui = nullptr;  // what ui.set and ui.language write; nullptr without a UI
    scene::Scene* const* screens = nullptr;  // the screens ui.get_field / ui.set_field act on
    uint32_t screen_count = 0;
    uint64_t seed = 1;            // seeds math.random
    double max_call_seconds = 1;  // one hook call longer than this is aborted (ZE0502)
};

// Compiles the file and runs its top level. Errors (ZE0500-ZE0502) carry file and line.
Script* load_script(const ScriptDesc& desc, DiagList* diags);
void destroy_script(Script* script);

// Each returns false and adds a diagnostic when the hook raises an error. Missing hooks are ok.
bool call_start(Script* script, DiagList* diags);
bool call_tick(Script* script, uint64_t tick, DiagList* diags);
bool call_ui(Script* script, const char* action, const char* screen, const char* node, DiagList* diags);

// Recompiles the file and re-runs its top level in the same environment, so hooks are replaced
// and globals the script does not reassign keep their values. On error the old code stays.
bool reload(Script* script, DiagList* diags);

const char* script_path(const Script* script);

}  // namespace zaro::script
