// GPU particles, compute side (render/particle_renderer.cpp): a ring buffer of sparks. `emit`
// writes the frame's new particles from the bursts the CPU described (random directions from a
// hash, so the CPU sends 64 bytes per burst instead of per particle); `update` integrates every
// live one. shaders/particles_draw.slang draws them. `Burst` mirrors render::ParticleBurst (64 bytes).

import particle;

struct Burst {
    float3 position;
    uint count;
    float4 color;  // linear rgb times intensity, opacity
    float2 speed;  // min, max
    float life;
    float size;
    float drag;
    float direction;  // radians; +x turns toward +z
    float spread;
    uint seed;
};

[[vk::binding(0, 0)]] ConstantBuffer<Sim> sim;
[[vk::binding(1, 0)]] RWStructuredBuffer<Particle> particles;
[[vk::binding(2, 0)]] StructuredBuffer<Burst> bursts;
[[vk::binding(3, 0)]] StructuredBuffer<uint> burst_first;  // index of each burst's first new particle

float hash01(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x & 0xffffffu) / 16777216.0;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void emit(uint3 id: SV_DispatchThreadID) {
    const uint i = id.x;
    if (i >= sim.emit_total || sim.burst_count == 0u) return;
    // The burst this particle belongs to: the last one starting at or before it.
    uint lo = 0u, hi = sim.burst_count - 1u;
    while (lo < hi) {
        const uint mid = (lo + hi + 1u) / 2u;
        if (burst_first[mid] <= i) lo = mid;
        else hi = mid - 1u;
    }
    const Burst b = bursts[lo];
    const uint k = (i - burst_first[lo]) * 4u + b.seed * 2654435761u;
    const float angle = b.direction + (hash01(k) - 0.5) * b.spread;
    const float speed = lerp(b.speed.x, b.speed.y, hash01(k + 1u));
    Particle p;
    p.position_age = float4(b.position, 0.0);
    p.velocity_life = float4(cos(angle) * speed, 0.0, sin(angle) * speed, b.life * lerp(0.6, 1.0, hash01(k + 2u)));
    p.color = b.color;
    p.size_drag = float4(b.size * lerp(0.7, 1.3, hash01(k + 3u)), b.drag, 0.0, 0.0);
    particles[(sim.head + i) % sim.capacity] = p;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void update(uint3 id: SV_DispatchThreadID) {
    const uint i = id.x;
    if (i >= sim.capacity) return;
    Particle p = particles[i];
    if (p.position_age.w >= p.velocity_life.w) return;
    const float dt = sim.forward.w;
    p.velocity_life.xyz *= exp(-p.size_drag.y * dt);
    p.position_age.xyz += p.velocity_life.xyz * dt;
    p.position_age.w += dt;
    particles[i] = p;
}
