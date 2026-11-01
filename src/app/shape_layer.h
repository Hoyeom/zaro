// The scene's glowing shapes: every Shape component (with its hit flash advanced on the tick clock
// and its trail sampled every tick), the crowd's CrowdShape look and the ArenaLook colours. Wires
// the shape components (model) to render::ShapeRenderer and the crowd renderer (view). Visual only:
// nothing here feeds the simulation (D-005).
#pragma once

#include "core/diag.h"
#include "render/crowd_renderer.h"
#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro::scene {
struct Scene;
}

namespace zaro::app {

struct ShapeLayer;

// Collects the scene's shape components and checks them: a CrowdShape beside a CrowdSprite, or a
// screen shape that follows the player, is ZE1100. Returns nullptr when the scene has none, or
// after adding diagnostics (*failed is then true).
ShapeLayer* create_shape_layer(scene::Scene* scene, DiagList* diags, bool* failed);
void destroy_shape_layer(ShapeLayer* layer);

// After each simulation tick: lowers every Shape's flash by flash_decay per tick and samples the
// trails at the tick's positions (the player's for following shapes).
void step_shapes(ShapeLayer* layer, uint64_t tick, float player_x, float player_z);

// The crowd renderer's look: whether the enemies are shapes (render::CrowdLook::shapes).
bool crowd_has_shapes(const ShapeLayer* layer);
// Fills the frame's crowd fields from the components as they are now: enemy shapes, arena colours,
// and draw_player (false when a Shape follows the player). keep_depth is set when world shapes
// follow. `scene` keeps pointers into the layer, valid until the next call.
void shape_crowd_scene(ShapeLayer* layer, render::CrowdScene* scene);

bool attach_shape_gpu(ShapeLayer* layer, rhi::Device* device, const char* shader_dir, rhi::TextureFormat format);
bool reload_shape_layer_shaders(ShapeLayer* layer);

// Records the "shapes" pass into `target` with the 3D view's camera (render::crowd_camera). Shapes
// that follow the player use its interpolated position; spin uses `seconds` of the tick clock.
// keep_depth keeps the depth buffer for a later pass (sprites).
void render_shape_layer(ShapeLayer* layer, const float view_proj[16], rhi::Texture* target, float player_x,
                        float player_z, float seconds, bool keep_depth);

}  // namespace zaro::app
