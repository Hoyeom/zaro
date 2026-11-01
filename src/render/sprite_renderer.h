// Sprite renderer: batched quads from sprite sheet atlases, in the 3D view (upright, facing the
// camera, depth-tested) and on the screen (pixels, blended in order). It draws plain data; the
// app turns Sprite components into instances (D-014). One draw call per batch.
#pragma once

#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro::render {

// 32 bytes. Mirrors `struct FrameInfo` in shaders/sprite.slang.
struct SpriteFrameInfo {
    float uv[4];     // u0, v0, u1, v1 in the atlas
    float size[2];   // pixels
    float pivot[2];  // 0..1 from the frame's top-left
};
static_assert(sizeof(SpriteFrameInfo) == 32, "must match shaders/sprite.slang");

enum SpriteFlags : uint32_t { kSpriteFlipX = 1u << 0 };

// 32 bytes. Mirrors `struct Instance` in shaders/sprite.slang.
struct SpriteInstance {
    float position[3];  // world metres, or screen pixels from the top-left (z unused)
    float scale;        // world: metres per frame pixel; screen: screen pixels per frame pixel
    uint32_t frame;     // index into the sheet's frames
    uint32_t color;     // sRGB RGBA8, red in the low byte
    uint32_t flags;     // SpriteFlags
    float pad;
};
static_assert(sizeof(SpriteInstance) == 32, "must match shaders/sprite.slang");

// `count` instances from `first`, all of one sheet.
struct SpriteBatch {
    uint32_t sheet;
    uint32_t first;
    uint32_t count;
    bool screen;
};

struct SpriteDraw {
    const SpriteInstance* instances = nullptr;
    uint32_t instance_count = 0;
    const SpriteBatch* batches = nullptr;
    uint32_t batch_count = 0;
    float view_proj[16] = {};  // the 3D view's camera (render::crowd_camera)
    float right[3] = {1.0f, 0.0f, 0.0f};
};

struct SpriteRenderer;

// shader_dir holds sprite.wgsl.
SpriteRenderer* create_sprite_renderer(rhi::Device* device, const char* shader_dir);
void destroy_sprite_renderer(SpriteRenderer* renderer);
bool reload_sprite_shaders(SpriteRenderer* renderer);

// Uploads a sheet: its atlas (RGBA8 sRGB, straight alpha, rows top to bottom) and its frames.
// Returns the index for SpriteBatch::sheet.
uint32_t add_sprite_sheet(SpriteRenderer* renderer, const uint8_t* rgba, uint32_t width, uint32_t height,
                          const SpriteFrameInfo* frames, uint32_t frame_count, const char* label);

// Records the "sprites" pass over what earlier passes drew. It loads their depth, so the 3D view's
// pass must keep it (CrowdScene::keep_depth). World batches are drawn first, then screen batches
// in order. Call between rhi::begin_frame and rhi::end_frame.
void render_sprites(SpriteRenderer* renderer, const SpriteDraw& draw);

}  // namespace zaro::render
