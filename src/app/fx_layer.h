// The scene's frame effects: the HDR frame with bloom (Bloom), screen shake and flash (ScreenFx),
// and GPU particles (ParticleEmitter, and CrowdFx for the crowd's hits). Wires the effect
// components (model) to render::PostRenderer and render::ParticleRenderer (view) like the sprites
// and the UI. Visual only: nothing here feeds the simulation (D-005).
#pragma once

#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro::scene {
struct Scene;
}
namespace zaro::sim {
struct Crowd;
}

namespace zaro::app {

struct FxLayer;
struct ShapeLayer;

// Particles alive at once at most; new ones replace the oldest.
constexpr uint32_t kParticleCapacity = 32768;

// nullptr when the scene has no effect components (the 3D view then draws straight into the frame).
FxLayer* create_fx_layer(scene::Scene* scene);
void destroy_fx_layer(FxLayer* layer);

// After each simulation tick: lowers ScreenFx shake and flash by their decay, turns the crowd's hits
// into CrowdFx sparks, shake, flash and a flash on `shapes` that follow the player, and runs every
// ParticleEmitter (its rate, and a burst it was given, which goes back to 0). A hit uses the CrowdFx
// on its kind's spawner entity (kind_ids, in kind order; D-036), else the scene's first CrowdFx. The
// values the components hold change the same way with or without a GPU; sparks are only queued with one.
void step_fx(FxLayer* layer, const sim::Crowd* crowd, ShapeLayer* shapes, const uint32_t* kind_ids,
             uint32_t kind_ids_count);

// The camera's shake offset (x, z metres) at `seconds` on the tick clock; zero without ScreenFx.
void fx_camera_offset(const FxLayer* layer, float seconds, float out[2]);

// Creates the GPU side and picks the HDR format (Bloom.format; auto prefers kRG11B10Float, D-030).
// False if a shader is missing.
bool attach_fx_gpu(FxLayer* layer, rhi::Device* device, const char* shader_dir);
bool reload_fx_shaders(FxLayer* layer);
// The format the 3D view's pipelines draw in: the HDR format, or kFrame without an HDR frame.
rhi::TextureFormat fx_scene_format(const FxLayer* layer);
// "rg11b10f", "rgba16f", or "none" without an HDR frame.
const char* fx_format_name(const FxLayer* layer);

// The target the 3D view's passes draw into this frame: the HDR frame, or nullptr (the frame
// itself) without one. Call after rhi::begin_frame.
rhi::Texture* fx_begin_frame(FxLayer* layer);
// The particle passes: integrates the sparks by `seconds` (since the last frame) and draws them
// into `target` with the 3D view's camera. Call after the 3D view's other passes.
void render_fx_particles(FxLayer* layer, const float view_proj[16], const float camera_forward[3],
                         rhi::Texture* target, float seconds);
// Bloom, tonemap and flash into the frame. Call after the 3D view's passes and before the UI.
void render_fx(FxLayer* layer);

}  // namespace zaro::app
