#include "render/shape_renderer.h"

#include "core/profile.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace zaro::render {

namespace {

// Mirrors `struct View` in shaders/shape.slang.
struct ShapeViewConstants {
    float view_proj[16];
    float screen[4];
};
static_assert(sizeof(ShapeViewConstants) == 80, "must match shaders/shape.slang");

float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }

}  // namespace

struct ShapeRenderer {
    rhi::Device* device = nullptr;
    char shader_dir[512] = {};
    rhi::TextureFormat format = rhi::TextureFormat::kFrame;
    uint32_t capacity = 0;

    rhi::Buffer* view = nullptr;
    rhi::Buffer* instances = nullptr;
    rhi::BindLayout* layout = nullptr;
    rhi::BindGroup* group = nullptr;
    rhi::RenderPipeline* world = nullptr;
    rhi::RenderPipeline* screen = nullptr;
};

namespace {

void ensure_capacity(ShapeRenderer* r, uint32_t count) {
    if (count <= r->capacity && r->instances) return;
    uint32_t capacity = r->capacity ? r->capacity : 1024;
    while (capacity < count) capacity *= 2;
    rhi::destroy_bind_group(r->group);
    rhi::destroy_buffer(r->instances);
    r->capacity = capacity;
    r->instances = rhi::create_buffer(r->device, (uint64_t)capacity * sizeof(ShapeInstance), rhi::kBufStorage,
                                      "shape instances");
    const rhi::BindEntry entries[2] = {{0, r->view}, {1, r->instances}};
    r->group = rhi::create_bind_group(r->device, r->layout, entries, 2);
}

}  // namespace

void shape_color(uint32_t c, float intensity, float out[4]) {
    out[0] = srgb_to_linear((float)(c & 255u) / 255.0f) * intensity;
    out[1] = srgb_to_linear((float)((c >> 8) & 255u) / 255.0f) * intensity;
    out[2] = srgb_to_linear((float)((c >> 16) & 255u) / 255.0f) * intensity;
    out[3] = (float)(c >> 24) / 255.0f;
}

bool reload_shape_shaders(ShapeRenderer* r) {
    char path[600];
    snprintf(path, sizeof(path), "%sshape.wgsl", r->shader_dir);
    rhi::Shader* shader = rhi::load_shader(r->device, path);
    if (!shader) return false;
    rhi::destroy_render_pipeline(r->world);
    rhi::destroy_render_pipeline(r->screen);
    rhi::RenderPipelineDesc d{"shapes", shader, "vs_shape", "fs_shape", r->layout};
    d.cull_back = false;
    d.alpha_blend = true;
    d.depth_write = false;
    d.format = r->format;
    r->world = rhi::create_render_pipeline(r->device, d);
    d.label = "screen shapes";
    d.depth = false;
    r->screen = rhi::create_render_pipeline(r->device, d);
    rhi::destroy_shader(shader);
    return true;
}

ShapeRenderer* create_shape_renderer(rhi::Device* device, const char* shader_dir, rhi::TextureFormat format) {
    ShapeRenderer* r = new ShapeRenderer();
    r->device = device;
    r->format = format;
    snprintf(r->shader_dir, sizeof(r->shader_dir), "%s", shader_dir);
    r->view = rhi::create_buffer(device, sizeof(ShapeViewConstants), rhi::kBufUniform, "shape view");
    using rhi::BindingType;
    const rhi::BindingDesc bindings[] = {
        {0, BindingType::kUniform, rhi::kStageVertex},
        {1, BindingType::kStorageRead, rhi::kStageVertex | rhi::kStageFragment},
    };
    r->layout = rhi::create_bind_layout(device, bindings, 2);
    ensure_capacity(r, 1);
    if (!reload_shape_shaders(r)) {
        destroy_shape_renderer(r);
        return nullptr;
    }
    return r;
}

void destroy_shape_renderer(ShapeRenderer* r) {
    if (!r) return;
    rhi::destroy_render_pipeline(r->world);
    rhi::destroy_render_pipeline(r->screen);
    rhi::destroy_bind_group(r->group);
    rhi::destroy_bind_layout(r->layout);
    rhi::destroy_buffer(r->instances);
    rhi::destroy_buffer(r->view);
    delete r;
}

void render_shapes(ShapeRenderer* r, const ShapeDraw& draw) {
    ZARO_ZONE("render.shapes");
    const uint32_t count = draw.world_count + draw.screen_count;
    if (!count) return;
    uint32_t width, height;
    rhi::target_size(r->device, &width, &height);
    ShapeViewConstants vc;
    memcpy(vc.view_proj, draw.view_proj, sizeof(vc.view_proj));
    vc.screen[0] = (float)width;
    vc.screen[1] = (float)height;
    vc.screen[2] = width ? 1.0f / (float)width : 0.0f;
    vc.screen[3] = height ? 1.0f / (float)height : 0.0f;
    rhi::write_buffer(r->device, r->view, 0, &vc, sizeof(vc));
    ensure_capacity(r, count);
    rhi::write_buffer(r->device, r->instances, 0, draw.instances, (uint64_t)count * sizeof(ShapeInstance));
    rhi::render_begin(r->device, "shapes", nullptr, rhi::kDepthLoad | (draw.keep_depth ? rhi::kDepthStore : 0u),
                      draw.target);
    if (draw.world_count) rhi::draw(r->device, r->world, r->group, 6, draw.world_count);
    if (draw.screen_count) rhi::draw(r->device, r->screen, r->group, 6, draw.screen_count, draw.world_count);
    rhi::render_end(r->device);
}

}  // namespace zaro::render
