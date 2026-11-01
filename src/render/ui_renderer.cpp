#include "render/ui_renderer.h"

#include "core/profile.h"

#include <stdio.h>

namespace zaro::render {

struct UiRenderer {
    rhi::Device* device = nullptr;
    char shader_dir[512] = {};
    uint32_t atlas_size = 0;
    uint32_t image_atlas_size = 0;
    uint32_t capacity = 0;  // quads the buffer holds

    rhi::Buffer* screen = nullptr;
    rhi::Buffer* quads = nullptr;
    rhi::Texture* atlas = nullptr;
    rhi::Texture* images = nullptr;
    rhi::Sampler* sampler = nullptr;
    rhi::BindLayout* layout = nullptr;
    rhi::BindGroup* group = nullptr;
    rhi::RenderPipeline* pipeline = nullptr;
};

namespace {

// (Re)creates the quad buffer for at least `count` quads, and the bind group that points at it.
void ensure_capacity(UiRenderer* r, uint32_t count) {
    if (count <= r->capacity && r->group) return;
    uint32_t capacity = r->capacity ? r->capacity : 1024;
    while (capacity < count) capacity *= 2;
    rhi::destroy_bind_group(r->group);
    rhi::destroy_buffer(r->quads);
    r->capacity = capacity;
    r->quads = rhi::create_buffer(r->device, (uint64_t)capacity * sizeof(UiQuad), rhi::kBufStorage, "ui quads");
    rhi::BindEntry entries[5] = {{0, r->screen}, {1, r->quads}, {2, nullptr}, {3, nullptr}, {4, nullptr}};
    entries[2].texture = r->atlas;
    entries[3].sampler = r->sampler;
    entries[4].texture = r->images;
    r->group = rhi::create_bind_group(r->device, r->layout, entries, 5);
}

void upload(rhi::Device* device, rhi::Texture* texture, const uint8_t* pixels, uint32_t size, uint32_t texel_bytes,
            uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    if (!pixels || x1 <= x0 || y1 <= y0) return;
    const uint8_t* first = pixels + ((size_t)y0 * size + x0) * texel_bytes;
    rhi::write_texture(device, texture, x0, y0, x1 - x0, y1 - y0, first, size * texel_bytes);
}

}  // namespace

bool reload_ui_shaders(UiRenderer* r) {
    char path[600];
    snprintf(path, sizeof(path), "%sui.wgsl", r->shader_dir);
    rhi::Shader* shader = rhi::load_shader(r->device, path);
    if (!shader) return false;
    rhi::destroy_render_pipeline(r->pipeline);
    rhi::RenderPipelineDesc d{"ui", shader, "vs_quad", "fs_quad", r->layout};
    d.cull_back = false;
    d.depth = false;
    d.alpha_blend = true;
    r->pipeline = rhi::create_render_pipeline(r->device, d);
    rhi::destroy_shader(shader);
    return true;
}

UiRenderer* create_ui_renderer(rhi::Device* device, const char* shader_dir, uint32_t atlas_size,
                               uint32_t image_atlas_size) {
    UiRenderer* r = new UiRenderer();
    r->device = device;
    snprintf(r->shader_dir, sizeof(r->shader_dir), "%s", shader_dir);
    r->atlas_size = atlas_size;
    r->image_atlas_size = image_atlas_size;
    r->screen = rhi::create_buffer(device, 16, rhi::kBufUniform, "ui screen");
    r->atlas = rhi::create_texture(device, atlas_size, atlas_size, rhi::TextureFormat::kR8, "glyph atlas");
    r->images = rhi::create_texture(device, image_atlas_size, image_atlas_size, rhi::TextureFormat::kRGBA8Srgb,
                                    "image atlas");
    r->sampler = rhi::create_sampler(device, true);
    using rhi::BindingType;
    const rhi::BindingDesc bindings[] = {
        {0, BindingType::kUniform, rhi::kStageVertex},
        {1, BindingType::kStorageRead, rhi::kStageVertex | rhi::kStageFragment},
        {2, BindingType::kTexture, rhi::kStageFragment},
        {3, BindingType::kSampler, rhi::kStageFragment},
        {4, BindingType::kTexture, rhi::kStageFragment},
    };
    r->layout = rhi::create_bind_layout(device, bindings, 5);
    ensure_capacity(r, 1);
    if (!reload_ui_shaders(r)) {
        destroy_ui_renderer(r);
        return nullptr;
    }
    return r;
}

void destroy_ui_renderer(UiRenderer* r) {
    if (!r) return;
    rhi::destroy_render_pipeline(r->pipeline);
    rhi::destroy_bind_group(r->group);
    rhi::destroy_bind_layout(r->layout);
    rhi::destroy_sampler(r->sampler);
    rhi::destroy_texture(r->atlas);
    rhi::destroy_texture(r->images);
    rhi::destroy_buffer(r->quads);
    rhi::destroy_buffer(r->screen);
    delete r;
}

void render_ui(UiRenderer* r, const UiFrame& frame, const float* clear_rgba) {
    ZARO_ZONE("render.ui");
    if (frame.atlas_size == r->atlas_size) {
        upload(r->device, r->atlas, frame.atlas, r->atlas_size, 1, frame.atlas_x0, frame.atlas_y0, frame.atlas_x1,
               frame.atlas_y1);
    }
    if (frame.image_atlas_size == r->image_atlas_size) {
        upload(r->device, r->images, frame.images, r->image_atlas_size, 4, frame.images_x0, frame.images_y0,
               frame.images_x1, frame.images_y1);
    }
    uint32_t width, height;
    rhi::target_size(r->device, &width, &height);
    const float screen[4] = {(float)width, (float)height, 1.0f / (float)width, 1.0f / (float)height};
    rhi::write_buffer(r->device, r->screen, 0, screen, sizeof(screen));
    ensure_capacity(r, frame.quad_count);
    if (frame.quad_count) {
        rhi::write_buffer(r->device, r->quads, 0, frame.quads, (uint64_t)frame.quad_count * sizeof(UiQuad));
    }
    rhi::render_begin(r->device, "ui", clear_rgba);
    if (frame.quad_count) rhi::draw(r->device, r->pipeline, r->group, 6, frame.quad_count);
    rhi::render_end(r->device);
}

}  // namespace zaro::render
