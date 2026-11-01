// Crowd renderer: the view. Draws whatever plain data it is given; it never sees sim types,
// so the simulation can change without touching rendering and vice versa.
#pragma once

#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro::render {

struct CrowdScene {
    const float* enemy_instances = nullptr;  // enemy_count * (x, z, heading_x, heading_z)
    uint32_t enemy_count = 0;
    float player_x = 0.0f;
    float player_z = 0.0f;
    float arena_size = 256.0f;
    float time_seconds = 0.0f;
    float zoom = 1.0f;  // >1 pulls the camera back
    bool keep_depth = false;  // keep the depth buffer for a later pass (sprites in the 3D view)
};

// The camera render_crowd uses for this scene and target size: view-projection (column-major,
// clip = view_proj * world) and the camera's right vector, for sprites drawn into the same view.
void crowd_camera(const CrowdScene& scene, uint32_t width, uint32_t height, float view_proj[16], float right[3]);

struct CrowdRenderer;

// shader_dir holds crowd_cull.wgsl and crowd_draw.wgsl (built from shaders/*.slang).
CrowdRenderer* create_crowd_renderer(rhi::Device* device, const char* shader_dir, uint32_t max_enemies,
                                     const uint32_t* obstacle_cells_xz, uint32_t obstacle_count);
void destroy_crowd_renderer(CrowdRenderer* renderer);

// Rebuilds pipelines from the shader files. Keeps the old pipelines if a file is missing.
bool reload_shaders(CrowdRenderer* renderer);

// Records the frame's passes. Call between rhi::begin_frame and rhi::end_frame.
void render_crowd(CrowdRenderer* renderer, const CrowdScene& scene);

}  // namespace zaro::render
