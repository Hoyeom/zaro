// Crowd scene: ground, obstacles, enemies (indirect, GPU-culled) and the player.
// Meshes are generated from SV_VertexID, so no vertex buffers are bound.
import frame;

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]] StructuredBuffer<float4> instances;
[[vk::binding(2, 0)]] StructuredBuffer<uint> visible;
[[vk::binding(3, 0)]] StructuredBuffer<uint2> obstacles;

static const float3 kSky = float3(0.06, 0.07, 0.09);
static const float3 kLightDir = float3(0.37, 0.86, 0.35);

struct VSOut {
    float4 pos : SV_Position;
    float3 world : WORLD;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

// Unit cube corner for vertex 0..35. Faces are CCW seen from outside (front face CCW).
void cube_vertex(uint vid, out float3 p, out float3 n) {
    static const float3 kNormals[6] = { float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0),
                                        float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1) };
    static const float3 kTangents[6] = { float3(0, 1, 0), float3(0, 1, 0), float3(0, 0, 1),
                                         float3(0, 0, 1), float3(1, 0, 0), float3(1, 0, 0) };
    static const float2 kCorners[6] = { float2(-1, -1), float2(1, -1), float2(1, 1),
                                        float2(-1, -1), float2(1, 1), float2(-1, 1) };
    const uint face = vid / 6;
    n = kNormals[face];
    const float3 t = kTangents[face];
    const float3 b = cross(n, t);
    const float2 c = kCorners[vid % 6];
    p = 0.5 * (n + t * c.x + b * c.y);
}

VSOut make_vertex(float3 world, float3 normal, float3 color) {
    VSOut o;
    o.pos = mul(frame.view_proj, float4(world, 1.0));
    o.world = world;
    o.normal = normal;
    o.color = color;
    return o;
}

float3 hue_tint(uint id) {
    uint h = id * 2654435761u;
    h ^= h >> 15;
    const float t = float(h & 255u) / 255.0;
    return lerp(float3(0.85, 0.16, 0.12), float3(0.95, 0.48, 0.10), t);
}

[shader("vertex")]
VSOut vs_enemy(uint vid: SV_VertexID, uint iid: SV_InstanceID) {
    const uint e = visible[iid];
    const float4 inst = instances[e];
    float3 p, n;
    cube_vertex(vid, p, n);
    const float3 fwd = float3(inst.z, 0.0, inst.w);
    const float3 right = float3(inst.w, 0.0, -inst.z);
    const float3 scale = float3(0.55, 0.9, 0.7);
    const float3 local = p * scale + float3(0.0, 0.45, 0.0);
    const float3 world = float3(inst.x, 0.0, inst.y) + right * local.x + float3(0, local.y, 0) + fwd * local.z;
    const float3 nw = right * n.x + float3(0, n.y, 0) + fwd * n.z;
    return make_vertex(world, nw, hue_tint(e));
}

[shader("vertex")]
VSOut vs_obstacle(uint vid: SV_VertexID, uint iid: SV_InstanceID) {
    const uint2 cell = obstacles[iid];
    float3 p, n;
    cube_vertex(vid, p, n);
    const float3 world = float3(float(cell.x) + 0.5, 1.0, float(cell.y) + 0.5) + p * float3(1.0, 2.0, 1.0);
    return make_vertex(world, n, float3(0.32, 0.34, 0.40));
}

[shader("vertex")]
VSOut vs_player(uint vid: SV_VertexID) {
    float3 p, n;
    cube_vertex(vid, p, n);
    const float3 world = float3(frame.player.x, 0.8, frame.player.y) + p * float3(0.9, 1.6, 0.9);
    return make_vertex(world, n, float3(0.20, 0.85, 1.00));
}

[shader("vertex")]
VSOut vs_ground(uint vid: SV_VertexID) {
    static const float2 kQuad[6] = { float2(0, 0), float2(0, 1), float2(1, 1),
                                     float2(0, 0), float2(1, 1), float2(1, 0) };
    const float size = frame.player.w;
    const float2 q = kQuad[vid] * size;
    return make_vertex(float3(q.x, 0.0, q.y), float3(0, 1, 0), float3(0.16, 0.17, 0.19));
}

float3 shade(VSOut i, float3 base) {
    const float ndl = max(dot(normalize(i.normal), kLightDir), 0.0);
    const float3 lit = base * (0.30 + 0.70 * ndl);
    const float dist = length(i.world - frame.camera.xyz);
    const float fog = saturate((dist - 60.0) / 140.0);
    return lerp(lit, kSky, fog);
}

[shader("fragment")]
float4 fs_lit(VSOut i) : SV_Target {
    return float4(shade(i, i.color), 1.0);
}

[shader("fragment")]
float4 fs_ground(VSOut i) : SV_Target {
    const int2 cell = int2(floor(i.world.xz / 4.0));
    const float checker = ((cell.x + cell.y) & 1) == 0 ? 1.0 : 0.82;
    const float2 f = frac(i.world.xz);
    const float line = (f.x < 0.03 || f.y < 0.03) ? 0.85 : 1.0;
    return float4(shade(i, i.color * checker * line), 1.0);
}
