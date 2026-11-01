// Sandboxed Luau game scripts (D-009).
//
// A script is a .luau file that may define two hooks:
//   function on_start()          -- once, after the scene is loaded
//   function on_tick(tick)       -- every simulation tick (60 Hz)
// and reaches the scene through one read-only global table. An entity is its id (a number) or
// its name (a string); ids are unique in the run and never reused (scene/world.h, D-031).
//   zaro.get(entity, component, field)         -> number | boolean | string | {x, y[, z[, w]]}
//   zaro.set(entity, component, field, value)  -- type- and range-checked through reflection
//   zaro.has(entity[, component])              -> boolean (false for a destroyed entity)
//   zaro.tick()                                -> current tick
//   zaro.log(...)                              -- prints "script: ..." to stderr
//   zaro.spawn(prefab[, {name = "..", parent = entity, <Component> = {field = value, ...}, ...}])  -> id
//       -- a prefab of [scene] prefabs, by its [prefab] name; component tables override fields
//          (or add the component). Without a name it is "<prefab>#<id>".
//   zaro.destroy(entity)                       -> true, or false for an id already destroyed
//       -- the entity is gone for scripts at once and removed at the end of the tick
//   zaro.alive(entity)                         -> boolean
//   zaro.find(name)                            -> id, or nil
//   zaro.id(entity), zaro.name(entity)         -> its id, its name
//   zaro.query([component, ...])               -> {id, ...}: live entities with all of them, in id order
//   zaro.count([component, ...])               -> how many zaro.query would return
//   zaro.parent(entity)                        -> parent id, or nil
//   zaro.set_parent(entity, parent | nil)      -- raises if it would make a loop
//   zaro.children(entity)                      -> {id, ...} in id order
//   zaro.world(entity)                         -> {x, y, z}, rotation, scale: its Transform combined
//                                                 with its parents' (a destroyed parent takes its children)
//   zaro.crowd_kill(x, z, radius[, max])       -> how many of the crowd's enemies within radius were swept
//                                                 away (they respawn at the edge and count as kills)
// The app copies the simulated player's position into the Transform of the PlayerController
// entity after every tick (when it has one), so its children follow the player, and queues the
// engine event "crowd_hit" {count, x, z} when enemies reached the player in a tick.
//   zaro.tween(entity, component, field, to, {time = s[, from, delay, ease, loop, count, done = fn]})
//       -> handle; eases a visual field on the tick clock (scene/tween.h has the eases and loops).
//          A tween on the same field replaces it, zaro.set on the field stops it, zaro.cancel too;
//          done(handle) runs in the tick it finishes.
// Timers and events run in the fixed tick (D-031). Seconds are tick time, rounded to whole ticks
// and at least one, so a timer made in on_start with 1.0 fires before on_tick(60).
//   zaro.after(seconds, fn)                    -> handle; fn(tick) once, before that tick's on_tick
//   zaro.every(seconds, fn[, times])           -> handle; fn(tick) each interval, `times` times or forever
//   zaro.on(event, fn)                         -> handle; fn(payload, event) for each such event
//   zaro.emit(event[, payload])                -- queued, and delivered after the hook that emitted it
//   zaro.cancel(handle)                        -> true if it stopped a timer or a subscription
// A tick runs the due timers (earliest first, then oldest), their events, on_tick, then its events.
// Events go out in emit order, to handlers in subscription order, including events that handlers
// emit; more than kMaxEventsPerTick in one tick is ZE1201. Hooks below deliver their events too.
// and the scene's UI through `ui` (view model in scene/ui_state.h, navigation in ui/navigator.h):
//   ui.set(key, value)       -- a view-model value (number or string); texts show it as {key}
//   ui.get(key)              -> the value, or nil
//   ui.get_field(screen, node, component, field)         -- like zaro.get on a shown screen's node
//   ui.set_field(screen, node, component, field, value)  -- like zaro.set
//   ui.language(code)        -- switch to one of the scene's languages
//   ui.open(screen[, {key = "..", wait = true, force = true, unskippable = true}])
//   ui.ready(screen)         -- an open with wait = true may start its entrance
//   ui.close(screen), ui.back(), ui.home(), ui.reset()
//   ui.state(screen)         -> "opening", "entering", "open", "closing" or "closed"
//   ui.tween(screen, node, component, field, to, options)  -- zaro.tween on a widget field
//   function on_ui(action, screen, node)   -- hook: a button with that action was clicked
//   function on_back(screen)               -- hook: back was pressed while `screen` had the input;
//                                             return true to take the press (nothing closes)
//   function on_screen(event, screen)      -- hook: "created" (a new instance, before its entrance),
//                                             "opened", "closed", "cancelled", "back_unhandled"
// There is no os, io, loadstring or require, globals of the standard library are read-only, and
// math.random is seeded from the scene so runs stay deterministic.
#pragma once

#include "core/diag.h"
#include "scene/scene.h"
#include "scene/ui_state.h"

#include <stdint.h>

namespace zaro::script {

constexpr uint32_t kEventNameCapacity = 64;
constexpr uint32_t kMaxEventsPerTick = 10000;

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
    void (*reset)(void* user);
    const char* (*state)(void* user, const char* name);  // "opening" ... "closed"
};

// The crowd behind zaro.crowd_kill, provided by the app (nullptr without one).
struct CrowdHost {
    void* user;
    uint32_t (*kill)(void* user, float x, float z, float radius, uint32_t max);
};

struct ScriptDesc {
    const char* path;
    scene::Scene* scene;          // the scene zaro.get / zaro.set act on
    scene::UiState* ui = nullptr;     // what ui.set writes; nullptr without a UI
    const UiHost* host = nullptr;  // carries out ui.open, ui.language, ...; nullptr without a UI
    const CrowdHost* crowd = nullptr;  // carries out zaro.crowd_kill; nullptr without a crowd
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
// Runs the `done` callbacks of finished tweens (scene::step_tweens) and the events they emit.
bool finish_tweens(Script* script, const uint32_t* handles, uint32_t count, DiagList* diags);
// Queues an event from the engine with a payload table of numbers (keys[i] = values[i]). The
// script's handlers get it after the next hook. False if the name is empty or too long.
bool emit_event(Script* script, const char* name, const char* const* keys, const double* values, uint32_t count);

// Recompiles the file and re-runs its top level in the same environment, so hooks are replaced
// and globals the script does not reassign keep their values. On error the old code stays.
bool reload(Script* script, DiagList* diags);

const char* script_path(const Script* script);

}  // namespace zaro::script
