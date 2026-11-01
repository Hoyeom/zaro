// GPU particles, render side (render/particle_renderer.cpp): each live particle of the ring buffer
// as an additive streak stretched along its motion (shaders/particles_sim.slang moves them).
import particle;

[[vk::binding(0, 0)]] ConstantBuffer<Sim> sim;
[[vk::binding(1, 0)]] StructuredBuffer<Particle> particles;

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : UV;  // -1..1 along (x) and across (y) the streak
    nointerpolation float3 color : COLOR;
    nointerpolation float aspect : ASPECT;  // length / width
};

static const float2 kCorners[6] = { float2(-1, -1), float2(1, -1), float2(1, 1),
                                    float2(-1, -1), float2(1, 1), float2(-1, 1) };

[shader("vertex")]
VSOut vs_particle(uint vid: SV_VertexID, uint iid: SV_InstanceID) {
    const Particle p = particles[iid];
    VSOut o;
    o.uv = kCorners[vid];
    o.color = float3(0.0);
    o.aspect = 1.0;
    if (p.position_age.w >= p.velocity_life.w) {
        o.pos = float4(2.0, 2.0, 2.0, 1.0);  // dead: outside the clip volume
        return o;
    }
    const float t = p.position_age.w / max(p.velocity_life.w, 1e-4);
    const float3 fwd = sim.forward.xyz;
    // The streak lies along the motion as the camera sees it, 40 ms of it long.
    float3 axis = p.velocity_life.xyz - dot(p.velocity_life.xyz, fwd) * fwd;
    const float speed = length(axis);
    axis = speed > 1e-4 ? axis / speed : normalize(cross(fwd, float3(0.0, 0.0, 1.0)));
    const float3 side = normalize(cross(fwd, axis));
    const float width = p.size_drag.x * (1.0 - 0.5 * t);
    const float len = width + speed * 0.04;
    const float3 world = p.position_age.xyz + axis * (o.uv.x * len * 0.5) + side * (o.uv.y * width * 0.5);
    o.pos = mul(sim.view_proj, float4(world, 1.0));
    const float fade = (1.0 - t) * (1.0 - t);
    o.color = p.color.rgb * p.color.a * fade;
    o.aspect = len / max(width, 1e-4);
    return o;
}

// A soft capsule: bright along its core, fading to nothing at its rim. Additive: alpha 0.
[shader("fragment")]
float4 fs_particle(VSOut i) : SV_Target {
    const float along = max(abs(i.uv.x) * i.aspect - (i.aspect - 1.0), 0.0);
    const float r = length(float2(along, i.uv.y));
    float a = saturate(1.0 - r);
    a *= a;
    return float4(i.color * a, 0.0);
}
