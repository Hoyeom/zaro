#include "render/post_renderer.h"

#include "core/profile.h"

#include <stdio.h>
#include <string.h>

namespace zaro::render {

namespace {

// Mirrors `struct Post` in shaders/post.slang (64 bytes).
struct PostConstants {
    float texel[4];  // 1 / width, 1 / height of the source; the same for the bloom texture
    float bloom[4];  // threshold, knee, intensity, radius
    float flash[4];  // linear rgb, amount
    float tone[4];   // exposure, unused
};
static_assert(sizeof(PostConstants) == 64, "must match shaders/post.slang");

// Prefilter, (levels - 1) downsamples, (levels - 1) upsamples, composite.
constexpr uint32_t kMaxPasses = kMaxBloomLevels * 2;

}  // namespace

struct PostRenderer {
    rhi::Device* device = nullptr;
    char shader_dir[512] = {};
    rhi::TextureFormat format = rhi::TextureFormat::kRGBA16Float;

    rhi::Sampler* sampler = nullptr;
    rhi::BindLayout* layout = nullptr;
    rhi::RenderPipeline* prefilter = nullptr;
    rhi::RenderPipeline* down = nullptr;
    rhi::RenderPipeline* up = nullptr;
    rhi::RenderPipeline* composite = nullptr;
    rhi::Buffer* constants[kMaxPasses] = {};

    // Targets, rebuilt when the frame's size or the number of levels changes.
    uint32_t width = 0, height = 0, levels = 0;
    rhi::Texture* scene = nullptr;
    rhi::Texture* level[kMaxBloomLevels] = {};
    uint32_t level_w[kMaxBloomLevels] = {}, level_h[kMaxBloomLevels] = {};
    rhi::BindGroup* groups[kMaxPasses] = {};
};

namespace {

void destroy_pipelines(PostRenderer* r) {
    rhi::destroy_render_pipeline(r->prefilter);
    rhi::destroy_render_pipeline(r->down);
    rhi::destroy_render_pipeline(r->up);
    rhi::destroy_render_pipeline(r->composite);
    r->prefilter = r->down = r->up = r->composite = nullptr;
}

void destroy_targets(PostRenderer* r) {
    for (rhi::BindGroup*& g : r->groups) {
        rhi::destroy_bind_group(g);
        g = nullptr;
    }
    for (rhi::Texture*& t : r->level) {
        rhi::destroy_texture(t);
        t = nullptr;
    }
    rhi::destroy_texture(r->scene);
    r->scene = nullptr;
    r->width = r->height = r->levels = 0;
}

rhi::BindGroup* make_group(PostRenderer* r, uint32_t pass, rhi::Texture* source, rhi::Texture* bloom) {
    rhi::BindEntry e[4] = {{0, r->constants[pass]}, {1, nullptr}, {2, nullptr}, {3, nullptr}};
    e[1].texture = source;
    e[2].sampler = r->sampler;
    e[3].texture = bloom;
    return rhi::create_bind_group(r->device, r->layout, e, 4);
}

// Pass order: 0 prefilter (scene -> level 0), 1..L-1 downsample (level i-1 -> i), then L-1
// upsamples (level i+1 -> i, from the smallest), and last the composite (scene + level 0 -> frame).
void ensure_targets(PostRenderer* r, uint32_t width, uint32_t height, uint32_t levels) {
    if (width == r->width && height == r->height && levels == r->levels && r->scene) return;
    destroy_targets(r);
    r->width = width;
    r->height = height;
    r->scene = rhi::create_render_texture(r->device, width, height, r->format, "hdr scene");
    uint32_t w = width, h = height, n = 0;
    while (n < levels) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        r->level[n] = rhi::create_render_texture(r->device, w, h, r->format, "bloom level");
        r->level_w[n] = w;
        r->level_h[n] = h;
        ++n;
        if (w < 8 || h < 8) break;  // smaller levels add nothing but passes
    }
    uint32_t pass = 0;
    r->groups[pass++] = make_group(r, 0, r->scene, r->scene);
    for (uint32_t i = 1; i < n; ++i, ++pass) r->groups[pass] = make_group(r, pass, r->level[i - 1], r->level[i - 1]);
    for (uint32_t i = n - 1; i-- > 0; ++pass) r->groups[pass] = make_group(r, pass, r->level[i + 1], r->level[i + 1]);
    r->groups[pass] = make_group(r, pass, r->scene, r->level[0]);
    r->levels = levels;  // the requested count, compared on the next call; built_levels() says how many fit
}

uint32_t built_levels(const PostRenderer* r) {
    uint32_t n = 0;
    while (n < kMaxBloomLevels && r->level[n]) ++n;
    return n;
}

void write_constants(PostRenderer* r, uint32_t pass, uint32_t src_w, uint32_t src_h, uint32_t bloom_w,
                     uint32_t bloom_h, const PostSettings& s) {
    PostConstants c = {};
    c.texel[0] = 1.0f / (float)src_w;
    c.texel[1] = 1.0f / (float)src_h;
    c.texel[2] = 1.0f / (float)bloom_w;
    c.texel[3] = 1.0f / (float)bloom_h;
    c.bloom[0] = s.bloom_threshold;
    c.bloom[1] = s.bloom_knee;
    c.bloom[2] = s.bloom_intensity;
    c.bloom[3] = s.bloom_radius;
    memcpy(c.flash, s.flash, sizeof(c.flash));
    c.tone[0] = s.exposure;
    rhi::write_buffer(r->device, r->constants[pass], 0, &c, sizeof(c));
}

}  // namespace

bool reload_post_shaders(PostRenderer* r) {
    char path[600];
    snprintf(path, sizeof(path), "%spost.wgsl", r->shader_dir);
    rhi::Shader* shader = rhi::load_shader(r->device, path);
    if (!shader) return false;
    destroy_pipelines(r);
    rhi::RenderPipelineDesc d{"bloom prefilter", shader, "vs_fullscreen", "fs_prefilter", r->layout};
    d.cull_back = false;
    d.depth = false;
    d.depth_buffer = false;
    d.format = r->format;
    r->prefilter = rhi::create_render_pipeline(r->device, d);
    d.label = "bloom down";
    d.fragment_entry = "fs_down";
    r->down = rhi::create_render_pipeline(r->device, d);
    d.label = "bloom up";
    d.fragment_entry = "fs_up";
    d.alpha_blend = true;  // the shader writes (up * radius, radius): a lerp toward the wider level
    r->up = rhi::create_render_pipeline(r->device, d);
    d.label = "tonemap";
    d.fragment_entry = "fs_composite";
    d.alpha_blend = false;
    d.format = rhi::TextureFormat::kFrame;
    r->composite = rhi::create_render_pipeline(r->device, d);
    rhi::destroy_shader(shader);
    return true;
}

PostRenderer* create_post_renderer(rhi::Device* device, const char* shader_dir, rhi::TextureFormat format) {
    PostRenderer* r = new PostRenderer();
    r->device = device;
    r->format = format;
    snprintf(r->shader_dir, sizeof(r->shader_dir), "%s", shader_dir);
    r->sampler = rhi::create_sampler(device, true);
    using rhi::BindingType;
    const rhi::BindingDesc bindings[] = {
        {0, BindingType::kUniform, rhi::kStageFragment},
        {1, BindingType::kTexture, rhi::kStageFragment},
        {2, BindingType::kSampler, rhi::kStageFragment},
        {3, BindingType::kTexture, rhi::kStageFragment},
    };
    r->layout = rhi::create_bind_layout(device, bindings, 4);
    for (rhi::Buffer*& b : r->constants) b = rhi::create_buffer(device, sizeof(PostConstants), rhi::kBufUniform, "post");
    if (!reload_post_shaders(r)) {
        destroy_post_renderer(r);
        return nullptr;
    }
    return r;
}

void destroy_post_renderer(PostRenderer* r) {
    if (!r) return;
    destroy_targets(r);
    destroy_pipelines(r);
    for (rhi::Buffer* b : r->constants) rhi::destroy_buffer(b);
    rhi::destroy_bind_layout(r->layout);
    rhi::destroy_sampler(r->sampler);
    delete r;
}

rhi::TextureFormat post_format(const PostRenderer* r) { return r->format; }

rhi::Texture* post_scene_target(PostRenderer* r, const PostSettings& s) {
    uint32_t w, h;
    rhi::target_size(r->device, &w, &h);
    const uint32_t levels = s.bloom_levels < 1 ? 1 : (s.bloom_levels > kMaxBloomLevels ? kMaxBloomLevels : s.bloom_levels);
    ensure_targets(r, w, h, levels);
    return r->scene;
}

void render_post(PostRenderer* r, const PostSettings& s) {
    ZARO_ZONE("render.post");
    if (!r->scene) return;  // post_scene_target was not called this frame
    const uint32_t w = r->width, h = r->height;
    const uint32_t n = built_levels(r);
    const bool bloom = s.bloom_intensity > 0.0f;

    uint32_t pass = 0;
    if (bloom) {
        write_constants(r, pass, w, h, w, h, s);
        rhi::render_begin(r->device, "bloom", nullptr, rhi::kDepthNone, r->level[0]);
        rhi::draw(r->device, r->prefilter, r->groups[pass++], 3, 1);
        rhi::render_end(r->device);
        for (uint32_t i = 1; i < n; ++i, ++pass) {
            write_constants(r, pass, r->level_w[i - 1], r->level_h[i - 1], r->level_w[i - 1], r->level_h[i - 1], s);
            rhi::render_begin(r->device, "bloom", nullptr, rhi::kDepthNone, r->level[i]);
            rhi::draw(r->device, r->down, r->groups[pass], 3, 1);
            rhi::render_end(r->device);
        }
        for (uint32_t i = n - 1; i-- > 0; ++pass) {
            write_constants(r, pass, r->level_w[i + 1], r->level_h[i + 1], r->level_w[i + 1], r->level_h[i + 1], s);
            rhi::render_begin(r->device, "bloom", nullptr, rhi::kDepthNone, r->level[i]);
            rhi::draw(r->device, r->up, r->groups[pass], 3, 1);
            rhi::render_end(r->device);
        }
    } else {
        pass = 2 * (n - 1) + 1;
    }
    write_constants(r, pass, w, h, r->level_w[0], r->level_h[0], s);
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    rhi::render_begin(r->device, "tonemap", black, rhi::kDepthNone);
    rhi::draw(r->device, r->composite, r->groups[pass], 3, 1);
    rhi::render_end(r->device);
}

}  // namespace zaro::render
