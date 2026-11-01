// Crowd scene: ground, obstacles, enemies (indirect, GPU-culled) and the player.
// Meshes are generated from SV_VertexID, so no vertex buffers are bound. Enemies are cubes,
// animated sprites from one sheet (render::CrowdSprites, `SpriteFrame` = render::SpriteFrameInfo),
// or glowing shapes (render::CrowdShapes). With arena colours (frame.ground.w = 1) the ground is
// dark with a glowing grid instead of the grey checkerboard.
import frame;
import shape_sdf;

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]] StructuredBuffer<float4> instances;
[[vk::binding(2, 0)]] StructuredBuffer<uint> visible;
[[vk::binding(3, 0)]] StructuredBuffer<uint2> obstacles;

struct SpriteFrame {
    float4 uv;     // u0, v0, u1, v1 in the atlas
    float2 size;   // pixels
    float2 pivot;  // 0..1 from the frame's top-left
};

[[vk::binding(4, 0)]] Texture2D<float4> sprite_atlas;  // sRGB, so samples are linear
[[vk::binding(5, 0)]] SamplerState sprite_sampler;
[[vk::binding(6, 0)]] StructuredBuffer<SpriteFrame> sprite_frames;  // the animation, in play order

static const float3 kSky = float3(0.06, 0.07, 0.09);

bool has_arena_colors() { return frame.ground.w > 0.5; }
float3 sky() { return has_arena_colors() ? frame.sky.rgb : kSky; }
float fog_amount(float3 world) { return saturate((length(world - frame.camera.xyz) - 60.0) / 140.0); }
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

struct SpriteOut {
    float4 pos : SV_Position;
    float2 uv : UV;
    float3 world : WORLD;
    nointerpolation float3 tint : TINT;
};

// An enemy as an upright sprite facing the camera. Each one starts the animation at its own phase
// (from its index) and is mirrored when it walks against the camera's right (the art faces right).
[shader("vertex")]
SpriteOut vs_enemy_sprite(uint vid: SV_VertexID, uint iid: SV_InstanceID) {
    static const float2 kCorners[6] = { float2(0, 0), float2(1, 0), float2(1, 1),
                                        float2(0, 0), float2(1, 1), float2(0, 1) };
    const uint e = visible[iid];
    const float4 inst = instances[e];
    const uint count = uint(frame.sprite.z);
    uint h = e * 2654435761u;
    h ^= h >> 13;
    const uint step = uint(frame.player.z * frame.sprite.y) + h;
    const SpriteFrame f = sprite_frames[step % count];
    const float2 c = kCorners[vid];
    const float2 offset = (c - f.pivot) * f.size * frame.sprite.x;
    const bool mirror = dot(inst.zw, frame.right.xz) < 0.0;
    const float u = mirror ? 1.0 - c.x : c.x;
    const float3 world = float3(inst.x, 0.0, inst.y) + frame.right.xyz * offset.x - float3(0.0, offset.y, 0.0);
    SpriteOut o;
    o.pos = mul(frame.view_proj, float4(world, 1.0));
    o.uv = lerp(f.uv.xy, f.uv.zw, float2(u, c.y));
    o.world = world;
    // A little variety: each enemy a slightly different shade.
    o.tint = lerp(float3(0.78, 0.74, 0.72), float3(1.0, 1.0, 1.0), float((h >> 8) & 255u) / 255.0);
    return o;
}

[shader("fragment")]
float4 fs_enemy_sprite(SpriteOut i) : SV_Target {
    const float4 t = sprite_atlas.Sample(sprite_sampler, i.uv);
    if (t.a < 0.5) discard;
    return float4(lerp(t.rgb * i.tint, sky(), fog_amount(i.world)), 1.0);
}

struct ShapeOut {
    float4 pos : SV_Position;
    float2 local : LOCAL;  // metres in the enemy's frame, +x the way it walks
    float3 world : WORLD;
    nointerpolation float3 color : COLOR;
};

// Turns c's hue by `turns` of the colour wheel (a rotation about the grey axis).
float3 rotate_hue(float3 c, float turns) {
    const float a = turns * 6.2831853;
    const float k = 0.57735027;
    const float3 axis = float3(k, k, k);
    return c * cos(a) + cross(axis, c) * sin(a) + axis * dot(axis, c) * (1.0 - cos(a));
}

// An enemy as a shape flat on the ground, its first corner along its heading.
[shader("vertex")]
ShapeOut vs_enemy_shape(uint vid: SV_VertexID, uint iid: SV_InstanceID) {
    static const float2 kCorners[6] = { float2(-1, -1), float2(1, -1), float2(1, 1),
                                        float2(-1, -1), float2(1, 1), float2(-1, 1) };
    const uint e = visible[iid];
    const float4 inst = instances[e];
    const uint kind = uint(frame.shape.x);
    const float2 size = float2(frame.shape.z);
    const float2 local = kCorners[vid] * (shape_extent(kind, size, frame.shape.w) + frame.shape_glow.x + 0.06);
    float2 fwd = inst.zw;
    fwd = dot(fwd, fwd) > 1e-6 ? normalize(fwd) : float2(1.0, 0.0);
    // Local +y is the heading turned 90 degrees toward +z, as for Shape components.
    const float3 world = float3(inst.x, 0.05, inst.y) + float3(fwd.x, 0.0, fwd.y) * local.x +
                         float3(-fwd.y, 0.0, fwd.x) * local.y;
    uint h = e * 2654435761u;
    h ^= h >> 15;
    const float jitter = (float(h & 1023u) / 1023.0 - 0.5) * 2.0 * frame.shape_glow.w;
    ShapeOut o;
    o.pos = mul(frame.view_proj, float4(world, 1.0));
    o.local = local;
    o.world = world;
    o.color = max(rotate_hue(frame.shape_color.rgb, jitter), float3(0.0));
    return o;
}

[shader("fragment")]
float4 fs_enemy_shape(ShapeOut i) : SV_Target {
    const uint kind = uint(frame.shape.x);
    const float d = shape_distance(kind, i.local, float2(frame.shape.z), frame.shape.w, frame.shape.y);
    const float4 c = float4(i.color * (1.0 - fog_amount(i.world)), frame.shape_color.a);
    return shape_shade(d, fwidth(d), frame.shape_glow.x, frame.shape_glow.y, c, frame.shape_glow.z > 0.5);
}

[shader("vertex")]
VSOut vs_obstacle(uint vid: SV_VertexID, uint iid: SV_InstanceID) {
    const uint2 cell = obstacles[iid];
    float3 p, n;
    cube_vertex(vid, p, n);
    const float3 world = float3(float(cell.x) + 0.5, 1.0, float(cell.y) + 0.5) + p * float3(1.0, 2.0, 1.0);
    return make_vertex(world, n, has_arena_colors() ? frame.obstacle.rgb : float3(0.32, 0.34, 0.40));
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
    return lerp(lit, sky(), fog_amount(i.world));
}

[shader("fragment")]
float4 fs_lit(VSOut i) : SV_Target {
    return float4(shade(i, i.color), 1.0);
}

[shader("fragment")]
float4 fs_ground(VSOut i) : SV_Target {
    if (has_arena_colors()) {
        // Distance to the nearest grid line in metres: a sharp line and a faint halo around it.
        const float spacing = frame.grid.w;
        const float2 f = abs(frac(i.world.xz / spacing + 0.5) - 0.5) * spacing;
        const float d = min(f.x, f.y);
        const float aa = max(fwidth(d), 1e-4);
        const float line = saturate(1.0 - (d - 0.03) / aa);
        const float halo = 0.25 * exp(-d / 0.12);
        const float3 c = frame.ground.rgb + frame.grid.rgb * (line + halo);
        return float4(lerp(c, sky(), fog_amount(i.world)), 1.0);
    }
    const int2 cell = int2(floor(i.world.xz / 4.0));
    const float checker = ((cell.x + cell.y) & 1) == 0 ? 1.0 : 0.82;
    const float2 f = frac(i.world.xz);
    const float line = (f.x < 0.03 || f.y < 0.03) ? 0.85 : 1.0;
    return float4(shade(i, i.color * checker * line), 1.0);
}
