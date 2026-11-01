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
// and the scene's UI through `ui` (view model in scene/ui_state.h, navigation in ui/navigator.h):
//   ui.set(key, value)       -- a view-model value (number or string); texts show it as {key}
//   ui.get(key)              -> the value, or nil
//   ui.get_field(screen, node, component, field)         -- like zaro.get on a shown screen's node
//   ui.set_field(screen, node, component, field, value)  -- like zaro.set
//   ui.language(code)        -- switch to one of the scene's languages
//   ui.open(screen[, {key = "..", wait = true, force = true, unskippable = true}])
//   ui.ready(screen)         -- an open with wait = true may start its entrance
//   ui.close(screen), ui.back(), ui.home(), ui.abort_all()
//   ui.state(screen)         -> "opening", "entering", "open", "closing" or "closed"
//   function on_ui(action, screen, node)   -- hook: a button with that action was clicked
//   function on_back(screen)               -- hook: back was pressed while `screen` had the input;
//                                             return true to take the press (nothing closes)
//   function on_screen(event, screen)      -- hook: "created" (a new instance, before its entrance),
//                                             "opened", "closed", "cancelled", "back_at_root"
// There is no os, io, loadstring or require, globals of the standard library are read-only, and
// math.random is seeded from the scene so runs stay deterministic.
#pragma once

#include "core/diag.h"
#include "scene/scene.h"
#include "scene/ui_state.h"

#include <stdint.h>

namespace zaro::script {

struct Script;

// The UI operations behind the `ui` table, provided by the app. Each returns false with a message in
// err (err_size bytes) when the request cannot be done; the binding raises it as a script error.
struct UiHost {
    void* user;
    // The topmost shown instance of a screen, or nullptr. `names` lists what is shown or can be
    // opened, for "did you mean".
    scene::Scene* (*find_screen)(void* user, const char* name);
    uint32_t (*screen_names)(void* user, const char** names, uint32_t max);
    bool (*language)(void* user, const char* code, char* err, uint32_t err_size);
    bool (*open)(void* user, const char* name, const char* key, bool wait, bool force, bool unskippable, char* err,
                 uint32_t err_size);
    bool (*ready)(void* user, const char* name);
    bool (*close)(void* user, const char* name);
    void (*back)(void* user);
    void (*home)(void* user);
    void (*abort_all)(void* user);
    const char* (*state)(void* user, const char* name);  // "opening" ... "closed"
};

struct ScriptDesc {
    const char* path;
    scene::Scene* scene;          // the scene zaro.get / zaro.set act on
    scene::UiState* ui = nullptr;     // what ui.set writes; nullptr without a UI
    const UiHost* host = nullptr;  // carries out ui.open, ui.language, ...; nullptr without a UI
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
// on_back: *claimed is true if the hook returned true.
bool call_back(Script* script, const char* screen, bool* claimed, DiagList* diags);
bool call_screen(Script* script, const char* event, const char* screen, DiagList* diags);

// Recompiles the file and re-runs its top level in the same environment, so hooks are replaced
// and globals the script does not reassign keep their values. On error the old code stays.
bool reload(Script* script, DiagList* diags);

const char* script_path(const Script* script);

}  // namespace zaro::script
