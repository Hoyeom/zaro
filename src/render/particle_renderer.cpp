#include "render/particle_renderer.h"

#include "core/profile.h"

#include <stdio.h>
#include <string.h>

namespace zaro::render {

namespace {

// Mirrors `struct Sim` in shaders/particle.slang (96 bytes).
struct ParticleConstants {
    float view_proj[16];
    float forward[4];  // camera forward xyz, seconds since the last frame
    uint32_t head;
    uint32_t emit_total;
    uint32_t capacity;
    uint32_t burst_count;
};
static_assert(sizeof(ParticleConstants) == 96, "must match shaders/particle.slang");

constexpr uint64_t kParticleBytes = 64;  // `struct Particle` in shaders/particle.slang

}  // namespace

struct ParticleRenderer {
    rhi::Device* device = nullptr;
    char shader_dir[512] = {};
    rhi::TextureFormat format = rhi::TextureFormat::kFrame;
    uint32_t capacity = 0;
    uint32_t head = 0;      // ring slot of the next new particle
    uint64_t emitted = 0;

    rhi::Buffer* constants = nullptr;
    rhi::Buffer* particles = nullptr;
    rhi::Buffer* bursts = nullptr;
    rhi::Buffer* first = nullptr;
    rhi::BindLayout* sim_layout = nullptr;
    rhi::BindLayout* draw_layout = nullptr;
    rhi::BindGroup* sim_group = nullptr;
    rhi::BindGroup* draw_group = nullptr;
    rhi::ComputePipeline* emit = nullptr;
    rhi::ComputePipeline* update = nullptr;
    rhi::RenderPipeline* draw = nullptr;
};

namespace {

void destroy_pipelines(ParticleRenderer* r) {
    rhi::destroy_compute_pipeline(r->emit);
    rhi::destroy_compute_pipeline(r->update);
    rhi::destroy_render_pipeline(r->draw);
    r->emit = r->update = nullptr;
    r->draw = nullptr;
}

}  // namespace

bool reload_particle_shaders(ParticleRenderer* r) {
    char path[600];
    snprintf(path, sizeof(path), "%sparticles_sim.wgsl", r->shader_dir);
    rhi::Shader* sim = rhi::load_shader(r->device, path);
    snprintf(path, sizeof(path), "%sparticles_draw.wgsl", r->shader_dir);
    rhi::Shader* draw = rhi::load_shader(r->device, path);
    if (!sim || !draw) {
        rhi::destroy_shader(sim);
        rhi::destroy_shader(draw);
        return false;
    }
    destroy_pipelines(r);
    r->emit = rhi::create_compute_pipeline(r->device, {"particles emit", sim, "emit", r->sim_layout});
    r->update = rhi::create_compute_pipeline(r->device, {"particles update", sim, "update", r->sim_layout});
    rhi::RenderPipelineDesc d{"particles", draw, "vs_particle", "fs_particle", r->draw_layout};
    d.cull_back = false;
    d.depth = false;
    d.depth_buffer = false;
    d.alpha_blend = true;  // the shader writes alpha 0: additive
    d.format = r->format;
    r->draw = rhi::create_render_pipeline(r->device, d);
    rhi::destroy_shader(sim);
    rhi::destroy_shader(draw);
    return true;
}

ParticleRenderer* create_particle_renderer(rhi::Device* device, const char* shader_dir, uint32_t capacity,
                                           rhi::TextureFormat format) {
    ParticleRenderer* r = new ParticleRenderer();
    r->device = device;
    r->format = format;
    r->capacity = capacity ? capacity : 1;
    snprintf(r->shader_dir, sizeof(r->shader_dir), "%s", shader_dir);
    r->constants = rhi::create_buffer(device, sizeof(ParticleConstants), rhi::kBufUniform, "particle constants");
    r->particles = rhi::create_buffer(device, r->capacity * kParticleBytes, rhi::kBufStorage, "particles");
    r->bursts = rhi::create_buffer(device, kMaxBurstsPerFrame * sizeof(ParticleBurst), rhi::kBufStorage,
                                   "particle bursts");
    r->first = rhi::create_buffer(device, kMaxBurstsPerFrame * 4ull, rhi::kBufStorage, "particle burst starts");
    using rhi::BindingType;
    const rhi::BindingDesc sim_bindings[] = {
        {0, BindingType::kUniform, rhi::kStageCompute},
        {1, BindingType::kStorageReadWrite, rhi::kStageCompute},
        {2, BindingType::kStorageRead, rhi::kStageCompute},
        {3, BindingType::kStorageRead, rhi::kStageCompute},
    };
    const rhi::BindingDesc draw_bindings[] = {
        {0, BindingType::kUniform, rhi::kStageVertex},
        {1, BindingType::kStorageRead, rhi::kStageVertex},
    };
    r->sim_layout = rhi::create_bind_layout(device, sim_bindings, 4);
    r->draw_layout = rhi::create_bind_layout(device, draw_bindings, 2);
    const rhi::BindEntry sim_entries[] = {{0, r->constants}, {1, r->particles}, {2, r->bursts}, {3, r->first}};
    const rhi::BindEntry draw_entries[] = {{0, r->constants}, {1, r->particles}};
    r->sim_group = rhi::create_bind_group(device, r->sim_layout, sim_entries, 4);
    r->draw_group = rhi::create_bind_group(device, r->draw_layout, draw_entries, 2);
    if (!reload_particle_shaders(r)) {
        destroy_particle_renderer(r);
        return nullptr;
    }
    return r;
}

void destroy_particle_renderer(ParticleRenderer* r) {
    if (!r) return;
    destroy_pipelines(r);
    rhi::destroy_bind_group(r->sim_group);
    rhi::destroy_bind_group(r->draw_group);
    rhi::destroy_bind_layout(r->sim_layout);
    rhi::destroy_bind_layout(r->draw_layout);
    rhi::destroy_buffer(r->constants);
    rhi::destroy_buffer(r->particles);
    rhi::destroy_buffer(r->bursts);
    rhi::destroy_buffer(r->first);
    delete r;
}

uint64_t particles_emitted(const ParticleRenderer* r) { return r->emitted; }

void render_particles(ParticleRenderer* r, const ParticleFrame& f) {
    ZARO_ZONE("render.particles");
    const uint32_t bursts = f.burst_count < kMaxBurstsPerFrame ? f.burst_count : kMaxBurstsPerFrame;
    uint32_t first[kMaxBurstsPerFrame];
    uint32_t total = 0;
    for (uint32_t i = 0; i < bursts; ++i) {
        first[i] = total;
        total += f.bursts[i].count;
    }
    // More than the ring holds in one frame would overwrite itself; keep the first `capacity`.
    if (total > r->capacity) total = r->capacity;
    if (!total && !r->emitted) return;  // nothing alive yet

    ParticleConstants c;
    memcpy(c.view_proj, f.view_proj, sizeof(c.view_proj));
    memcpy(c.forward, f.camera_forward, sizeof(float) * 3);
    c.forward[3] = f.seconds;
    c.head = r->head;
    c.emit_total = total;
    c.capacity = r->capacity;
    c.burst_count = bursts;
    rhi::write_buffer(r->device, r->constants, 0, &c, sizeof(c));
    if (bursts) {
        rhi::write_buffer(r->device, r->bursts, 0, f.bursts, (uint64_t)bursts * sizeof(ParticleBurst));
        rhi::write_buffer(r->device, r->first, 0, first, bursts * 4ull);
    }

    // Integrate what lives, then add the new ones (they start where their burst is).
    rhi::compute_begin(r->device, "particles");
    rhi::compute_dispatch(r->device, r->update, r->sim_group, (r->capacity + 63) / 64);
    if (total) rhi::compute_dispatch(r->device, r->emit, r->sim_group, (total + 63) / 64);
    rhi::compute_end(r->device);
    r->head = (r->head + total) % r->capacity;
    r->emitted += total;

    const uint64_t drawn = r->emitted < r->capacity ? r->emitted : r->capacity;
    rhi::render_begin(r->device, "particles", nullptr, rhi::kDepthNone, f.target);
    rhi::draw(r->device, r->draw, r->draw_group, 6, (uint32_t)drawn);
    rhi::render_end(r->device);
}

}  // namespace zaro::render
