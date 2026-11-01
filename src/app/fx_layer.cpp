#include "app/fx_layer.h"

#include "core/log.h"
#include "render/post_renderer.h"
#include "scene/scene.h"
#include "sim/fx_components.h"

namespace zaro::app {

struct FxLayer {
    scene::Scene* scene = nullptr;
    rhi::Device* device = nullptr;
    render::PostRenderer* post = nullptr;
    rhi::TextureFormat format = rhi::TextureFormat::kFrame;
    render::PostSettings settings;
};

namespace {

// The frame's settings from the scene's components as they are now (scripts may change them).
render::PostSettings post_settings(const FxLayer* l) {
    render::PostSettings s;
    if (const auto* b = (const sim::Bloom*)scene::first_component(l->scene, &sim::kBloomDesc, nullptr)) {
        s.bloom_threshold = b->threshold;
        s.bloom_knee = b->knee;
        s.bloom_intensity = b->enabled ? b->intensity : 0.0f;
        s.bloom_radius = b->radius;
        s.bloom_levels = b->levels;
        s.exposure = b->exposure;
    }
    return s;
}

}  // namespace

FxLayer* create_fx_layer(scene::Scene* scene) {
    if (!scene || !scene::first_component(scene, &sim::kBloomDesc, nullptr)) return nullptr;
    FxLayer* l = new FxLayer();
    l->scene = scene;
    return l;
}

void destroy_fx_layer(FxLayer* l) {
    if (!l) return;
    render::destroy_post_renderer(l->post);
    delete l;
}

bool attach_fx_gpu(FxLayer* l, rhi::Device* device, const char* shader_dir) {
    if (!l) return true;
    l->device = device;
    const auto* b = (const sim::Bloom*)scene::first_component(l->scene, &sim::kBloomDesc, nullptr);
    const uint32_t wanted = b ? b->format : sim::kHdrAuto;
    const bool rg11b10 = rhi::supports_format(device, rhi::TextureFormat::kRG11B10Float);
    if (wanted == sim::kHdrRg11b10f && !rg11b10) log_info("bloom: rg11b10f is not renderable here, using rgba16f");
    l->format = (wanted != sim::kHdrRgba16f && rg11b10) ? rhi::TextureFormat::kRG11B10Float
                                                        : rhi::TextureFormat::kRGBA16Float;
    l->post = render::create_post_renderer(device, shader_dir, l->format);
    return l->post != nullptr;
}

bool reload_fx_shaders(FxLayer* l) { return !l || !l->post || render::reload_post_shaders(l->post); }

rhi::TextureFormat fx_scene_format(const FxLayer* l) { return l ? l->format : rhi::TextureFormat::kFrame; }

const char* fx_format_name(const FxLayer* l) {
    if (!l) return "none";
    return l->format == rhi::TextureFormat::kRG11B10Float ? "rg11b10f" : "rgba16f";
}

rhi::Texture* fx_begin_frame(FxLayer* l) {
    if (!l || !l->post) return nullptr;
    l->settings = post_settings(l);
    return render::post_scene_target(l->post, l->settings);
}

void render_fx(FxLayer* l) {
    if (!l || !l->post) return;
    render::render_post(l->post, l->settings);
}

}  // namespace zaro::app
