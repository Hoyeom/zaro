// GPU particles: sparks for hits and deaths (P6, D-029). The CPU only describes bursts (where, how
// many, what colour and speed); a compute pass writes each burst's particles into a ring buffer
// with hashed random directions and integrates every live particle, and a render pass draws them as
// additive glowing streaks stretched along their motion. Visual only: nothing reads them back, and
// nothing in the simulation depends on them (D-005).
#pragma once

#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro::render {

// 64 bytes. Mirrors `struct Burst` in shaders/particles.slang.
struct ParticleBurst {
    float position[3];  // world metres
    uint32_t count;     // particles
    float color[4];     // linear rgb times intensity, opacity
    float speed[2];     // min, max metres per second
    float life;         // seconds (each particle lives 60..100% of it)
    float size;         // streak width in metres
    float drag;         // velocity lost per second (exponential)
    float direction;    // radians from +x toward -z
    float spread;       // radians around the direction; 2 pi: every way
    uint32_t seed;
};
static_assert(sizeof(ParticleBurst) == 64, "must match shaders/particles.slang");

constexpr uint32_t kMaxBurstsPerFrame = 256;

struct ParticleFrame {
    const ParticleBurst* bursts = nullptr;  // new this frame; at most kMaxBurstsPerFrame are used
    uint32_t burst_count = 0;
    float seconds = 0.0f;      // time since the last frame, for integrating
    float view_proj[16] = {};  // the 3D view's camera (render::crowd_camera)
    float camera_forward[3] = {0.0f, -1.0f, 0.0f};
    rhi::Texture* target = nullptr;  // nullptr draws into the frame; else the 3D view's HDR target
};

struct ParticleRenderer;

// shader_dir holds particles.wgsl. `capacity` is the ring buffer's size: the most particles alive at
// once (new ones replace the oldest). `format` is the format of ParticleFrame::target.
ParticleRenderer* create_particle_renderer(rhi::Device* device, const char* shader_dir, uint32_t capacity,
                                           rhi::TextureFormat format = rhi::TextureFormat::kFrame);
void destroy_particle_renderer(ParticleRenderer* renderer);
bool reload_particle_shaders(ParticleRenderer* renderer);

// Records the "particles" compute pass (emit, then integrate) and the "particles" render pass over
// what earlier passes drew (no depth test). Call between rhi::begin_frame and rhi::end_frame.
void render_particles(ParticleRenderer* renderer, const ParticleFrame& frame);

// Particles emitted since creation (for tests and stats).
uint64_t particles_emitted(const ParticleRenderer* renderer);

}  // namespace zaro::render
