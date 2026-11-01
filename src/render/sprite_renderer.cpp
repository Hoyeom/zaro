#include "render/sprite_renderer.h"

#include "core/profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::render {

namespace {

// Mirrors `struct View` in shaders/sprite.slang.
struct ViewConstants {
    float view_proj[16];
    float right[4];
    float screen[4];
};
static_assert(sizeof(ViewConstants) == 96, "must match shaders/sprite.slang");

constexpr uint32_t kMaxSheets = 64;

}  // namespace

// A sheet on the GPU. Outside the anonymous namespace, as SpriteRenderer holds an array of them.
struct SpriteSheetGpu {
    rhi::Texture* texture = nullptr;
    rhi::Buffer* frames = nullptr;
    rhi::BindGroup* group = nullptr;
};

struct SpriteRenderer {
    rhi::Device* device = nullptr;
    char shader_dir[512] = {};
    uint32_t capacity = 0;  // instances the buffer holds

    rhi::Buffer* view = nullptr;
    rhi::Buffer* instances = nullptr;
    rhi::Sampler* sampler = nullptr;
    rhi::BindLayout* layout = nullptr;
    rhi::RenderPipeline* world = nullptr;
    rhi::RenderPipeline* screen = nullptr;
    SpriteSheetGpu sheets[kMaxSheets];
    uint32_t sheet_count = 0;
};

namespace {

void make_group(SpriteRenderer* r, SpriteSheetGpu& s) {
    rhi::destroy_bind_group(s.group);
    rhi::BindEntry entries[5] = {{0, r->view}, {1, r->instances}, {2, s.frames}, {3, nullptr}, {4, nullptr}};
    entries[3].texture = s.texture;
    entries[4].sampler = r->sampler;
    s.group = rhi::create_bind_group(r->device, r->layout, entries, 5);
}

// (Re)creates the instance buffer for at least `count` instances, and every sheet's bind group.
void ensure_capacity(SpriteRenderer* r, uint32_t count) {
    if (count <= r->capacity && r->instances) return;
    uint32_t capacity = r->capacity ? r->capacity : 1024;
    while (capacity < count) capacity *= 2;
    rhi::destroy_buffer(r->instances);
    r->capacity = capacity;
    r->instances = rhi::create_buffer(r->device, (uint64_t)capacity * sizeof(SpriteInstance), rhi::kBufStorage,
                                      "sprite instances");
    for (uint32_t i = 0; i < r->sheet_count; ++i) make_group(r, r->sheets[i]);
}

}  // namespace

bool reload_sprite_shaders(SpriteRenderer* r) {
    char path[600];
    snprintf(path, sizeof(path), "%ssprite.wgsl", r->shader_dir);
    rhi::Shader* shader = rhi::load_shader(r->device, path);
    if (!shader) return false;
    rhi::destroy_render_pipeline(r->world);
    rhi::destroy_render_pipeline(r->screen);
    rhi::RenderPipelineDesc d{"sprites", shader, "vs_world", "fs_cutout", r->layout};
    d.cull_back = false;
    r->world = rhi::create_render_pipeline(r->device, d);
    d = {"screen sprites", shader, "vs_screen", "fs_blend", r->layout};
    d.cull_back = false;
    d.depth = false;
    d.alpha_blend = true;
    r->screen = rhi::create_render_pipeline(r->device, d);
    rhi::destroy_shader(shader);
    return true;
}

SpriteRenderer* create_sprite_renderer(rhi::Device* device, const char* shader_dir) {
    SpriteRenderer* r = new SpriteRenderer();
    r->device = device;
    snprintf(r->shader_dir, sizeof(r->shader_dir), "%s", shader_dir);
    r->view = rhi::create_buffer(device, sizeof(ViewConstants), rhi::kBufUniform, "sprite view");
    r->sampler = rhi::create_sampler(device, true);
    using rhi::BindingType;
    const rhi::BindingDesc bindings[] = {
        {0, BindingType::kUniform, rhi::kStageVertex},
        {1, BindingType::kStorageRead, rhi::kStageVertex},
        {2, BindingType::kStorageRead, rhi::kStageVertex},
        {3, BindingType::kTexture, rhi::kStageFragment},
        {4, BindingType::kSampler, rhi::kStageFragment},
    };
    r->layout = rhi::create_bind_layout(device, bindings, 5);
    ensure_capacity(r, 1);
    if (!reload_sprite_shaders(r)) {
        destroy_sprite_renderer(r);
        return nullptr;
    }
    return r;
}

void destroy_sprite_renderer(SpriteRenderer* r) {
    if (!r) return;
    for (uint32_t i = 0; i < r->sheet_count; ++i) {
        rhi::destroy_bind_group(r->sheets[i].group);
        rhi::destroy_buffer(r->sheets[i].frames);
        rhi::destroy_texture(r->sheets[i].texture);
    }
    rhi::destroy_render_pipeline(r->world);
    rhi::destroy_render_pipeline(r->screen);
    rhi::destroy_bind_layout(r->layout);
    rhi::destroy_sampler(r->sampler);
    rhi::destroy_buffer(r->instances);
    rhi::destroy_buffer(r->view);
    delete r;
}

uint32_t add_sprite_sheet(SpriteRenderer* r, const uint8_t* rgba, uint32_t width, uint32_t height,
                          const SpriteFrameInfo* frames, uint32_t frame_count, const char* label) {
    if (r->sheet_count == kMaxSheets) return 0;  // callers stay under the limit (app/sprite_layer)
    SpriteSheetGpu& s = r->sheets[r->sheet_count];
    s.texture = rhi::create_texture(r->device, width, height, rhi::TextureFormat::kRGBA8Srgb, label);
    rhi::write_texture(r->device, s.texture, 0, 0, width, height, rgba, width * 4);
    const uint32_t n = frame_count ? frame_count : 1;
    s.frames = rhi::create_buffer(r->device, (uint64_t)n * sizeof(SpriteFrameInfo), rhi::kBufStorage, label);
    if (frame_count) rhi::write_buffer(r->device, s.frames, 0, frames, (uint64_t)frame_count * sizeof(SpriteFrameInfo));
    make_group(r, s);
    return r->sheet_count++;
}

void render_sprites(SpriteRenderer* r, const SpriteDraw& draw) {
    ZARO_ZONE("render.sprites");
    uint32_t width, height;
    rhi::target_size(r->device, &width, &height);
    ViewConstants vc;
    memcpy(vc.view_proj, draw.view_proj, sizeof(vc.view_proj));
    vc.right[0] = draw.right[0];
    vc.right[1] = draw.right[1];
    vc.right[2] = draw.right[2];
    vc.right[3] = 0.0f;
    vc.screen[0] = (float)width;
    vc.screen[1] = (float)height;
    vc.screen[2] = width ? 1.0f / (float)width : 0.0f;
    vc.screen[3] = height ? 1.0f / (float)height : 0.0f;
    rhi::write_buffer(r->device, r->view, 0, &vc, sizeof(vc));
    ensure_capacity(r, draw.instance_count);
    if (draw.instance_count) {
        rhi::write_buffer(r->device, r->instances, 0, draw.instances,
                          (uint64_t)draw.instance_count * sizeof(SpriteInstance));
    }
    rhi::render_begin(r->device, "sprites", nullptr, rhi::kDepthLoad);
    for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < draw.batch_count; ++i) {
            const SpriteBatch& b = draw.batches[i];
            if (b.screen != (pass == 1) || !b.count || b.sheet >= r->sheet_count) continue;
            if (b.first + b.count > draw.instance_count) continue;
            rhi::draw(r->device, b.screen ? r->screen : r->world, r->sheets[b.sheet].group, 6, b.count, b.first);
        }
    }
    rhi::render_end(r->device);
}

}  // namespace zaro::render
