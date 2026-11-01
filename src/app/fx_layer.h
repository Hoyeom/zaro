// The scene's frame effects: the HDR frame with bloom (Bloom). Wires the effect components (model)
// to render::PostRenderer (view) like the sprites and the UI. Visual only (D-005).
#pragma once

#include "rhi/rhi.h"

namespace zaro::scene {
struct Scene;
}

namespace zaro::app {

struct FxLayer;

// nullptr when the scene has no effect components (the 3D view then draws straight into the frame).
FxLayer* create_fx_layer(scene::Scene* scene);
void destroy_fx_layer(FxLayer* layer);

// Creates the GPU side and picks the HDR format (Bloom.format; auto prefers kRG11B10Float, D-030).
// False if a shader is missing.
bool attach_fx_gpu(FxLayer* layer, rhi::Device* device, const char* shader_dir);
bool reload_fx_shaders(FxLayer* layer);
// The format the 3D view's pipelines draw in: the HDR format, or kFrame without effects.
rhi::TextureFormat fx_scene_format(const FxLayer* layer);
const char* fx_format_name(const FxLayer* layer);

// The target the 3D view's passes draw into this frame: the HDR frame, or nullptr (the frame
// itself) without effects. Call after rhi::begin_frame.
rhi::Texture* fx_begin_frame(FxLayer* layer);
// Bloom and tonemap into the frame. Call after the 3D view's passes and before the UI.
void render_fx(FxLayer* layer);

}  // namespace zaro::app
