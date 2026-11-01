// Tweens (D-031): a visual field eased from one value to another over seconds of tick time.
//
// In a scene file, on an entity, or at the top level of a prefab (each entity spawned from it gets
// the tween when it is spawned):
//   [[entity.tween]]                 # [[tween]] in a prefab
//   component = "Sprite"             # a visual component of the entity (`"visual": true` in zaro schema)
//   field = "scale"                  # an f32, vec2, vec3, vec4 or color field
//   to = 1.5                         # a number, [x, y, ...] for vectors, "#rrggbb[aa]" for colours
//   from = 1.0                       # optional; default: the field's value when the tween starts
//   time = 0.4                       # seconds, more than 0
//   delay = 0.0                      # optional seconds before it starts
//   ease = "out_cubic"               # optional, default "linear"; the names are kEaseNames
//   loop = "pingpong"                # optional: none (default), repeat or pingpong
//   count = 4                        # optional with a loop: runs before it stops (a pingpong run is
//                                    # one way); 0 or none = forever
// Scripts start tweens with zaro.tween and ui.tween (script/script.h).
//
// Tweens advance on the tick clock and are applied at the end of each tick, after the simulation,
// so they look the same at any frame rate and never feed gameplay: they may only change visual
// components (reflect::is_visual), which the state hash leaves out.
#pragma once

#include "core/reflect.h"

#include <stdint.h>

namespace zaro::scene {

struct Scene;
struct Entity;

enum Ease : uint8_t {
    kEaseLinear,
    kEaseInQuad, kEaseOutQuad, kEaseInOutQuad,
    kEaseInCubic, kEaseOutCubic, kEaseInOutCubic,
    kEaseInQuart, kEaseOutQuart, kEaseInOutQuart,
    kEaseInSine, kEaseOutSine, kEaseInOutSine,
    kEaseInExpo, kEaseOutExpo, kEaseInOutExpo,
    kEaseInBack, kEaseOutBack, kEaseInOutBack,
    kEaseInElastic, kEaseOutElastic, kEaseInOutElastic,
    kEaseInBounce, kEaseOutBounce, kEaseInOutBounce,
    kEaseCount
};
// "linear", "in_quad", "out_quad", "in_out_quad", ... "in_out_bounce"; nullptr-terminated.
extern const char* const kEaseNames[];

enum TweenLoop : uint8_t { kLoopNone, kLoopRepeat, kLoopPingPong };
extern const char* const kLoopNames[];  // "none", "repeat", "pingpong"; nullptr-terminated

// What to tween and how, as a file or a script gave it.
struct TweenSpec {
    const reflect::ComponentDesc* component;
    const reflect::FieldDesc* field;
    float to[4];
    float from[4];
    bool has_from;
    double time;   // seconds
    double delay;  // seconds
    uint8_t ease;
    uint8_t loop;
    uint32_t count;  // runs with a loop; 0 = forever
};

// The eased progress for p in [0, 1] (deterministic math, core/detmath.h).
double ease(uint8_t curve, double p);
int find_ease(const char* name);  // -1 if unknown

// Why a field cannot be tweened, or nullptr when it can: the component must be visual and the
// field an f32, vector or colour.
const char* tween_target_error(const reflect::ComponentDesc* component, const reflect::FieldDesc* field);
// Reads a field's value into lanes (colours as 0..255 per channel). Returns the lane count.
uint32_t read_lanes(const reflect::FieldDesc* field, const void* component_data, float lanes[4]);

// Starts a tween on a scene entity, or (entity_id 0) on a node of a screen. A tween already on the
// same field of the same target is replaced. `handle` identifies script tweens (0 for files).
void start_tween(Scene* scene, const TweenSpec& spec, uint32_t entity_id, const char* screen, const char* node,
                 uint32_t handle, uint64_t tick);
// Stops the tween with this handle; false if there is none.
bool cancel_tween(Scene* scene, uint32_t handle);
// Stops tweens on a field (a script set it), on a scene entity or (entity_id 0) a screen node.
void stop_tweens(Scene* scene, uint32_t entity_id, const char* screen, const char* node,
                 const reflect::ComponentDesc* component, const reflect::FieldDesc* field);

// The screens tweens can reach, provided by the app (a shown screen's document, or nullptr).
typedef Scene* (*FindScreenFn)(void* user, const char* name);

// Writes every tween's value for this tick. Tweens whose entity, screen or node is gone are
// dropped. Finished script tweens put their handles in `done` (up to max_done); returns how many.
uint32_t step_tweens(Scene* scene, uint64_t tick, FindScreenFn find_screen, void* user, uint32_t* done,
                     uint32_t max_done);

void free_tweens(Scene* scene);

}  // namespace zaro::scene
