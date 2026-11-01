#include "app/fx_layer.h"

#include "app/shape_layer.h"
#include "core/log.h"
#include "render/particle_renderer.h"
#include "render/post_renderer.h"
#include "render/shape_renderer.h"
#include "scene/scene.h"
#include "sim/crowd.h"
#include "sim/fx_components.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

namespace {

constexpr float kFullCircle = 6.2831853f;
constexpr float kRadiansPerDegree = kFullCircle / 360.0f;
constexpr uint32_t kMaxHitBurstsPerTick = 64;  // more hits in one tick share the shake but not sparks
constexpr float kSparkHeight = 0.3f;

}  // namespace

// Outside the anonymous namespace, as FxLayer holds them.
struct EmitterRecord {
    sim::ParticleEmitter* emitter;
    float carry;  // fraction of a spark left over from earlier ticks
};

struct FxLayer {
    scene::Scene* scene = nullptr;
    bool hdr = false;  // Bloom or ScreenFx: render through the HDR frame and the tonemap
    EmitterRecord* emitters = nullptr;
    uint32_t emitter_count = 0;
    uint64_t generation = 0;  // the scene's generation the emitters were collected for (D-031)
    uint64_t ticks = 0;

    rhi::Device* device = nullptr;
    render::PostRenderer* post = nullptr;
    render::ParticleRenderer* particles = nullptr;
    rhi::TextureFormat format = rhi::TextureFormat::kFrame;
    render::PostSettings settings;
    render::ParticleBurst bursts[render::kMaxBurstsPerFrame];
    uint32_t burst_count = 0;
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
    } else {
        s.bloom_intensity = 0.0f;  // ScreenFx alone: the tonemap and the flash, no glow
    }
    if (const auto* f = (const sim::ScreenFx*)scene::first_component(l->scene, &sim::kScreenFxDesc, nullptr)) {
        render::shape_color(f->flash_color, 1.0f, s.flash);
        s.flash[3] = f->flash;
    }
    return s;
}

// Queues a burst for the next frame, when there is a GPU to draw it and room in the frame's list.
void queue_burst(FxLayer* l, float x, float y, float z, uint32_t count, uint32_t color, float intensity, float speed,
                 float life, float size, float drag, float direction, float spread) {
    if (!l->particles || !count || l->burst_count == render::kMaxBurstsPerFrame) return;
    render::ParticleBurst& b = l->bursts[l->burst_count];
    b.position[0] = x;
    b.position[1] = y;
    b.position[2] = z;
    b.count = count;
    render::shape_color(color, intensity, b.color);
    b.speed[0] = speed * 0.4f;
    b.speed[1] = speed;
    b.life = life;
    b.size = size;
    b.drag = drag;
    b.direction = direction;
    b.spread = spread;
    b.seed = (uint32_t)(l->ticks * 2654435761u) ^ (l->burst_count * 40503u + 1u);
    l->burst_count++;
}

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

bool prefab_has_emitter(const scene::Scene* s) {
    for (uint32_t i = 0; i < s->prefab_count; ++i) {
        for (uint32_t c = 0; c < s->prefabs[i].component_count; ++c) {
            if (s->prefabs[i].components[c].desc == &sim::kParticleEmitterDesc) return true;
        }
    }
    return false;
}

// Collects the emitters again after scripts spawned or removed entities (Scene::generation
// changed), keeping the carry of emitters that are still there; their data does not move.
void sync_emitters(FxLayer* l) {
    if (l->generation == l->scene->generation) return;
    l->generation = l->scene->generation;
    const scene::Scene* s = l->scene;
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->entity_count; ++i) count += scene::get_component(&s->entities[i], &sim::kParticleEmitterDesc) ? 1 : 0;
    EmitterRecord* old = l->emitters;
    const uint32_t old_count = l->emitter_count;
    l->emitters = (EmitterRecord*)calloc(count ? count : 1, sizeof(EmitterRecord));
    l->emitter_count = 0;
    uint32_t j = 0;
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        if (s->entities[i].destroyed) continue;
        auto* e = (sim::ParticleEmitter*)scene::get_component(&s->entities[i], &sim::kParticleEmitterDesc);
        if (!e) continue;
        EmitterRecord& r = l->emitters[l->emitter_count++];
        uint32_t k = j;
        while (k < old_count && old[k].emitter != e) ++k;
        if (k < old_count) {
            r = old[k];
            j = k + 1;
        } else {
            r.emitter = e;
        }
    }
    free(old);
}

}  // namespace

FxLayer* create_fx_layer(scene::Scene* scene) {
    if (!scene) return nullptr;
    const bool hdr = scene::first_component(scene, &sim::kBloomDesc, nullptr) ||
                     scene::first_component(scene, &sim::kScreenFxDesc, nullptr);
    uint32_t emitters = 0;
    for (uint32_t i = 0; i < scene->entity_count; ++i) {
        emitters += scene::get_component(&scene->entities[i], &sim::kParticleEmitterDesc) ? 1 : 0;
    }
    const bool crowd_fx = scene::first_component(scene, &sim::kCrowdFxDesc, nullptr) != nullptr;
    const bool spawnable = prefab_has_emitter(scene);
    if (!hdr && !emitters && !crowd_fx && !spawnable) return nullptr;
    FxLayer* l = new FxLayer();
    l->scene = scene;
    l->generation = scene->generation;
    l->hdr = hdr;
    l->emitters = (EmitterRecord*)calloc(emitters ? emitters : 1, sizeof(EmitterRecord));
    for (uint32_t i = 0; i < scene->entity_count; ++i) {
        scene::ComponentData* c =
            (scene::ComponentData*)scene::find_component_data(&scene->entities[i], &sim::kParticleEmitterDesc);
        if (c) l->emitters[l->emitter_count++].emitter = (sim::ParticleEmitter*)c->data;
    }
    return l;
}

void destroy_fx_layer(FxLayer* l) {
    if (!l) return;
    render::destroy_post_renderer(l->post);
    render::destroy_particle_renderer(l->particles);
    free(l->emitters);
    delete l;
}

void step_fx(FxLayer* l, const sim::Crowd* crowd, ShapeLayer* shapes) {
    if (!l) return;
    sync_emitters(l);
    l->ticks++;
    const float dt = sim::kTickSeconds;
    auto* screen = (sim::ScreenFx*)scene::first_component_mut(l->scene, &sim::kScreenFxDesc);
    if (screen) {
        screen->shake = clamp01(screen->shake - screen->shake_decay * dt);
        screen->flash = clamp01(screen->flash - screen->flash_decay * dt);
    }
    float px = 0.0f, pz = 0.0f;
    if (crowd) sim::player_position(crowd, 1.0f, &px, &pz);

    if (const auto* fx = (const sim::CrowdFx*)scene::first_component(l->scene, &sim::kCrowdFxDesc, nullptr)) {
        const float* xz = nullptr;
        const uint32_t hits = crowd ? sim::last_hits(crowd, &xz) : 0;
        for (uint32_t i = 0; i < hits && i < kMaxHitBurstsPerTick; ++i) {
            queue_burst(l, xz[i * 2], kSparkHeight, xz[i * 2 + 1], fx->particles, fx->color, fx->intensity, fx->speed,
                        fx->life, fx->size, 3.0f, 0.0f, kFullCircle);
        }
        if (hits) {
            if (screen) {
                screen->shake = clamp01(screen->shake + fx->shake * (float)hits);
                screen->flash = clamp01(screen->flash + fx->flash * (float)hits);
            }
            if (fx->player_flash > 0.0f) flash_player_shapes(shapes, fx->player_flash);
        }
    }

    for (uint32_t i = 0; i < l->emitter_count; ++i) {
        EmitterRecord& r = l->emitters[i];
        sim::ParticleEmitter& e = *r.emitter;
        uint32_t count = e.burst;
        e.burst = 0;
        if (e.enabled && e.rate > 0.0f) {
            r.carry += e.rate * dt;
            const uint32_t whole = (uint32_t)r.carry;
            r.carry -= (float)whole;
            count += whole;
        }
        const bool follow = e.follow == sim::kEmitterFollowPlayer;
        queue_burst(l, e.position[0] + (follow ? px : 0.0f), e.position[1], e.position[2] + (follow ? pz : 0.0f), count,
                    e.color, e.intensity, e.speed, e.life, e.size, e.drag, e.direction * kRadiansPerDegree, e.spread * kRadiansPerDegree);
    }
}

void fx_camera_offset(const FxLayer* l, float seconds, float out[2]) {
    out[0] = out[1] = 0.0f;
    if (!l) return;
    const auto* f = (const sim::ScreenFx*)scene::first_component(l->scene, &sim::kScreenFxDesc, nullptr);
    if (!f || f->shake <= 0.0f) return;
    // Two incommensurate sines per axis: smooth, never repeating within a shake.
    const float a = f->shake_strength * f->shake * f->shake;
    out[0] = a * 0.5f * (sinf(seconds * 37.1f) + sinf(seconds * 59.3f + 1.7f));
    out[1] = a * 0.5f * (sinf(seconds * 41.9f + 0.5f) + sinf(seconds * 67.7f + 2.3f));
}

bool attach_fx_gpu(FxLayer* l, rhi::Device* device, const char* shader_dir) {
    if (!l) return true;
    l->device = device;
    if (l->hdr) {
        const auto* b = (const sim::Bloom*)scene::first_component(l->scene, &sim::kBloomDesc, nullptr);
        const uint32_t wanted = b ? b->format : sim::kHdrAuto;
        const bool rg11b10 = rhi::supports_format(device, rhi::TextureFormat::kRG11B10Float);
        if (wanted == sim::kHdrRg11b10f && !rg11b10) log_info("bloom: rg11b10f is not renderable here, using rgba16f");
        l->format = (wanted != sim::kHdrRgba16f && rg11b10) ? rhi::TextureFormat::kRG11B10Float
                                                            : rhi::TextureFormat::kRGBA16Float;
        l->post = render::create_post_renderer(device, shader_dir, l->format);
        if (!l->post) return false;
    }
    if (l->emitter_count || prefab_has_emitter(l->scene) || scene::first_component(l->scene, &sim::kCrowdFxDesc, nullptr)) {
        l->particles = render::create_particle_renderer(device, shader_dir, kParticleCapacity, l->format);
        if (!l->particles) return false;
    }
    return true;
}

bool reload_fx_shaders(FxLayer* l) {
    if (!l) return true;
    const bool post = !l->post || render::reload_post_shaders(l->post);
    const bool particles = !l->particles || render::reload_particle_shaders(l->particles);
    return post && particles;
}

rhi::TextureFormat fx_scene_format(const FxLayer* l) { return l ? l->format : rhi::TextureFormat::kFrame; }

const char* fx_format_name(const FxLayer* l) {
    if (!l || !l->post) return "none";
    return l->format == rhi::TextureFormat::kRG11B10Float ? "rg11b10f" : "rgba16f";
}

rhi::Texture* fx_begin_frame(FxLayer* l) {
    if (!l || !l->post) return nullptr;
    l->settings = post_settings(l);
    return render::post_scene_target(l->post, l->settings);
}

void render_fx_particles(FxLayer* l, const float view_proj[16], const float camera_forward[3], rhi::Texture* target,
                         float seconds) {
    if (!l || !l->particles) return;
    render::ParticleFrame f;
    f.bursts = l->bursts;
    f.burst_count = l->burst_count;
    f.seconds = seconds;
    memcpy(f.view_proj, view_proj, sizeof(f.view_proj));
    memcpy(f.camera_forward, camera_forward, sizeof(f.camera_forward));
    f.target = target;
    render::render_particles(l->particles, f);
    l->burst_count = 0;
}

void render_fx(FxLayer* l) {
    if (!l || !l->post) return;
    render::render_post(l->post, l->settings);
}

}  // namespace zaro::app
