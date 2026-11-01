// Crowd renderer: the view. Draws whatever plain data it is given; it never sees sim types,
// so the simulation can change without touching rendering and vice versa.
#pragma once

#include "render/sprite_renderer.h"
#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro::render {

// Enemies drawn as glowing shapes (shaders/shape_sdf.slang) lying on the ground, the first corner
// pointing the way each one walks. Colours are linear RGB times intensity.
struct CrowdShapes {
    uint32_t kind = 3;  // render::ShapeKind
    uint32_t sides = 3;
    float size = 0.8f;  // diameter, metres
    float thickness = 0.08f;
    float color[4] = {1.0f, 0.1f, 0.2f, 1.0f};
    float glow = 0.25f;
    float glow_strength = 0.5f;
    float hue_jitter = 0.0f;
    bool additive = true;
};

// The arena's colours instead of the grey checkerboard; linear RGB (times intensity).
struct ArenaColors {
    float ground[3] = {};
    float grid[3] = {};
    float grid_spacing = 4.0f;
    float obstacle[3] = {};
    float sky[3] = {};
};

struct CrowdScene {
    const float* enemy_instances = nullptr;  // enemy_count * (x, z, heading_x, heading_z)
    uint32_t enemy_count = 0;
    float player_x = 0.0f;
    float player_z = 0.0f;
    float arena_size = 256.0f;
    float time_seconds = 0.0f;
    float zoom = 1.0f;  // >1 pulls the camera back
    bool keep_depth = false;  // keep the depth buffer for a later pass (sprites in the 3D view)
    rhi::Texture* target = nullptr;  // nullptr draws into the frame; else an HDR target (PostRenderer)
    bool draw_player = true;  // false when something else (a Shape following it) draws the player
    const CrowdShapes* enemy_shapes = nullptr;  // the enemies' look when created with shapes
    const ArenaColors* arena = nullptr;  // nullptr: the grey checkerboard
};

// The camera render_crowd uses for this scene and target size: view-projection (column-major,
// clip = view_proj * world) and the camera's right vector, for sprites drawn into the same view.
void crowd_camera(const CrowdScene& scene, uint32_t width, uint32_t height, float view_proj[16], float right[3]);

// Enemies drawn as upright, camera-facing sprites instead of cubes: one sheet's atlas and the frames
// of one animation in play order. Each enemy plays it from its own phase and faces the way it walks;
// the animation runs on the GPU from CrowdScene::time_seconds. Copied at creation.
struct CrowdSprites {
    const uint8_t* rgba = nullptr;  // atlas, sRGB RGBA8, straight alpha
    uint32_t width = 0, height = 0;
    const SpriteFrameInfo* frames = nullptr;
    uint32_t frame_count = 0;
    float fps = 10.0f;
    float metres_per_pixel = 0.01f;
};

// How the enemies look and where the passes draw: cubes by default, sprites (copied at creation), or
// shapes (CrowdScene::enemy_shapes gives their look every frame). `format` is CrowdScene::target's.
struct CrowdLook {
    const CrowdSprites* sprites = nullptr;
    bool shapes = false;
    rhi::TextureFormat format = rhi::TextureFormat::kFrame;
};

struct CrowdRenderer;

// shader_dir holds crowd_cull.wgsl and crowd_draw.wgsl (built from shaders/*.slang).
CrowdRenderer* create_crowd_renderer(rhi::Device* device, const char* shader_dir, uint32_t max_enemies,
                                     const uint32_t* obstacle_cells_xz, uint32_t obstacle_count,
                                     const CrowdLook& look = CrowdLook{});
void destroy_crowd_renderer(CrowdRenderer* renderer);

// Rebuilds pipelines from the shader files. Keeps the old pipelines if a file is missing.
bool reload_shaders(CrowdRenderer* renderer);

// Records the frame's passes. Call between rhi::begin_frame and rhi::end_frame.
void render_crowd(CrowdRenderer* renderer, const CrowdScene& scene);

}  // namespace zaro::render
