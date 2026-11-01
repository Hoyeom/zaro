// The scene's sprites: the sheets its Sprite components name, the animation of
// every Sprite (advanced on the tick clock), and their drawing in the 3D view and on the screen.
// Wires the Sprite components (model) to render::SpriteRenderer (view) like the crowd and the UI.
#pragma once

#include "core/diag.h"
#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro::scene {
struct Scene;
struct SpriteSheet;
}  // namespace zaro::scene

namespace zaro::app {

struct SpriteLayer;

// Loads every sheet the scene's sprite components name (sizes and frames, no pixels) and checks
// their animations and frames. Returns nullptr when the scene has no sprites, or after adding
// diagnostics (*failed is then true).
SpriteLayer* create_sprite_layer(scene::Scene* scene, DiagList* diags, bool* failed);
void destroy_sprite_layer(SpriteLayer* layer);

// Sets every animated Sprite's `frame` for this tick. An animation starts when a sprite first
// names it (at load, or when a script changes `animation`); an unknown name is a ZE0902
// diagnostic, reported once, and the sprite keeps its frame; then this returns false.
bool step_sprites(SpriteLayer* layer, uint64_t tick, DiagList* diags);

// SVG sheets (D-028): cooks the sheet's PNG when it is missing or its content hash changed, and
// writes the new hash next to it. Only the zaro tool can cook; a runtime without the cook (Android)
// reports a missing PNG as ZE0901. *cooked says whether it drew. Other sheets are left alone.
bool ensure_sheet_art(const scene::SpriteSheet* sheet, bool force, DiagList* diags, bool* cooked);
// Every SVG sheet of the layer; false if any failed.
bool prepare_sprite_art(SpriteLayer* layer, DiagList* diags);
// `zaro check`: the SVG files of every SVG sheet, without drawing them. True when the build has no
// cook (there is nothing to check against).
bool check_sprite_art(const SpriteLayer* layer, DiagList* diags);

// The sheets the layer loaded.
uint32_t sprite_sheet_count(const SpriteLayer* layer);
const scene::SpriteSheet* sprite_sheet_at(const SpriteLayer* layer, uint32_t index);

// Creates the GPU side: decodes each sheet's image and uploads it. False if a shader or an image
// is missing.
bool attach_sprite_gpu(SpriteLayer* layer, rhi::Device* device, const char* shader_dir);
bool reload_sprite_layer_shaders(SpriteLayer* layer);
// True if there are sprites to draw in the 3D view (its pass must then keep its depth).
bool has_world_sprites(const SpriteLayer* layer);

// Records the "sprites" pass with the 3D view's camera (render::crowd_camera). Call after the
// scene's passes and before the UI.
void render_sprite_layer(SpriteLayer* layer, const float view_proj[16], const float right[3]);

}  // namespace zaro::app
