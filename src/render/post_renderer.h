// Post-processing of an HDR frame (D-030). The 3D view's passes draw into a linear HDR target
// (post_scene_target) instead of the frame. render_post then blooms what is brighter than a
// threshold, tonemaps the sum into the frame and adds a full-screen flash; the UI is drawn over that.
// It draws plain data and never sees the simulation (D-005).
//
// Bloom is a chain of half-size levels: the first pass keeps only light above the threshold (with a
// soft knee) while it downsamples, each next pass downsamples with a 13-tap filter, then the chain
// is walked back up with a 3x3 tent filter, each level blended into the one above by `radius`.
#pragma once

#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro::render {

constexpr uint32_t kMaxBloomLevels = 8;

struct PostSettings {
    float bloom_threshold = 1.0f;  // linear brightness where glow starts
    float bloom_knee = 0.5f;       // 0: hard cut at the threshold; 1: glow fades in from 0
    float bloom_intensity = 0.8f;  // 0 skips the bloom passes
    float bloom_radius = 0.7f;     // 0..1: how much the wider levels spread into the narrower
    uint32_t bloom_levels = 5;     // half-size levels, at most kMaxBloomLevels
    float exposure = 1.0f;         // multiplies the scene before tonemapping
    float flash[4] = {};           // linear RGB and amount (0..1) added over the frame
};

struct PostRenderer;

// shader_dir holds post.wgsl. `format` is the HDR target's format (kRGBA16Float or kRG11B10Float).
PostRenderer* create_post_renderer(rhi::Device* device, const char* shader_dir, rhi::TextureFormat format);
void destroy_post_renderer(PostRenderer* renderer);
bool reload_post_shaders(PostRenderer* renderer);

// The format scene pipelines that draw into the target are created with.
rhi::TextureFormat post_format(const PostRenderer* renderer);
// The HDR target for this frame, the size of the frame (recreated when that or the settings' level
// count changes). Call after rhi::begin_frame and before the scene's passes.
rhi::Texture* post_scene_target(PostRenderer* renderer, const PostSettings& settings);

// Records the bloom passes and the tonemap into the frame. Call after the scene's passes, with the
// settings given to post_scene_target.
void render_post(PostRenderer* renderer, const PostSettings& settings);

}  // namespace zaro::render
