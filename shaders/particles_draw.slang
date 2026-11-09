// GPU particles, render side (render/particle_renderer.cpp): each live particle of the ring buffer
// as an additive streak stretched along its motion, or as a solid piece that spins and shrinks
// (D-040; shaders/particles_sim.slang moves them). Output is premultiplied: alpha 0 adds light.
import particle;

[[vk::binding(0, 0)]] ConstantBuffer<Sim> sim;
[[vk::binding(1, 0)]] StructuredBuffer<Particle> particles;

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : UV;  // spark: -1..1 along (x) and across (y) the streak; piece: 1 at its half width
    nointerpolation float4 color : COLOR;
    nointerpolation float aspect : ASPECT;  // spark: length / width
    nointerpolation uint shape : SHAPE;
};

static const float2 kCorners[6] = { float2(-1, -1), float2(1, -1), float2(1, 1),
                                    float2(-1, -1), float2(1, 1), float2(-1, 1) };
// The quad around a piece, in half widths: a triangle's corner reaches 1.155, and the edge needs a
// pixel beyond it to fade out.
static const float kPieceQuad = 1.3;

[shader("vertex")]
VSOut vs_particle(uint vid: SV_VertexID, uint iid: SV_InstanceID) {
    const Particle p = particles[iid];
    VSOut o;
    o.uv = kCorners[vid];
    o.color = float4(0.0);
    o.aspect = 1.0;
    o.shape = uint(p.size_drag.z + 0.5);
    if (p.position_age.w >= p.velocity_life.w) {
        o.pos = float4(2.0, 2.0, 2.0, 1.0);  // dead: outside the clip volume
        return o;
    }
    const float t = p.position_age.w / max(p.velocity_life.w, 1e-4);
    const float3 fwd = sim.forward.xyz;
    if (o.shape != 0u) {
        // Faces the camera, turned from an angle of its own, and shrinks to nothing at full colour.
        const float3 near_up = abs(fwd.z) > 0.99 ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
        const float3 right = normalize(cross(fwd, near_up));
        const float3 up = cross(right, fwd);
        const float angle = frac(float(iid) * 0.618034) * 6.283185 + p.size_drag.w * p.position_age.w;
        const float c = cos(angle), s = sin(angle);
        o.uv = kCorners[vid] * kPieceQuad;
        const float half_width = p.size_drag.x * 0.5 * (1.0 - t);
        const float3 offset = right * (c * o.uv.x - s * o.uv.y) + up * (s * o.uv.x + c * o.uv.y);
        o.pos = mul(sim.view_proj, float4(p.position_age.xyz + offset * half_width, 1.0));
        o.color = float4(p.color.rgb * p.color.a, p.color.a);
        return o;
    }
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
    o.color = float4(p.color.rgb * p.color.a * fade, 0.0);
    o.aspect = len / max(width, 1e-4);
    return o;
}

// Signed distance to a piece's edge, in half widths.
float piece_distance(uint shape, float2 uv) {
    if (shape == 1u) return max(abs(uv.x), abs(uv.y)) - 1.0;
    if (shape == 2u) return length(uv) - 1.0;
    // An equilateral triangle, side 2, around its centre.
    return max(abs(uv.x) * 0.866025 + uv.y * 0.5, -uv.y) - 0.57735;
}

[shader("fragment")]
float4 fs_particle(VSOut i) : SV_Target {
    // A solid piece: a sharp, antialiased edge. fwidth stays outside the branch on the shape, which
    // varies between draws of one pass.
    const float d = piece_distance(i.shape, i.uv);
    const float cover = saturate(0.5 - d / max(fwidth(d), 1e-4));
    if (i.shape != 0u) return i.color * cover;
    // A soft capsule: bright along its core, fading to nothing at its rim. Additive: alpha 0.
    const float along = max(abs(i.uv.x) * i.aspect - (i.aspect - 1.0), 0.0);
    const float r = length(float2(along, i.uv.y));
    float a = saturate(1.0 - r);
    a *= a;
    return float4(i.color.rgb * a, 0.0);
}
