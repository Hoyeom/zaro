// Sprites: quads cut from one sheet's atlas, one instance per sprite and one draw per batch.
// World sprites stand upright in the 3D view and turn to face the camera; they are depth-tested
// and cut out at half alpha, so a crowd needs no sorting. Screen sprites are pixels over the
// frame, alpha-blended in order. `Instance` and `FrameInfo` mirror render::SpriteInstance and
// render::SpriteFrameInfo (src/render/sprite_renderer.h, 32 bytes each); keep them in step.

struct View {
    float4x4 view_proj;  // column-major, clip = view_proj * world
    float4 right;        // the camera's right vector (xyz)
    float4 screen;       // width, height, 1 / width, 1 / height
};

struct FrameInfo {
    float4 uv;     // u0, v0, u1, v1 in the atlas
    float2 size;   // pixels
    float2 pivot;  // 0..1 from the frame's top-left
};

struct Instance {
    float3 position;  // world metres, or screen pixels (z unused)
    float scale;      // world: metres per frame pixel; screen: screen pixels per frame pixel
    uint frame;
    uint color;  // sRGB RGBA8, red in the low byte, straight alpha
    uint flags;  // 1: mirror horizontally
    float pad;
};

[[vk::binding(0, 0)]] ConstantBuffer<View> view;
[[vk::binding(1, 0)]] StructuredBuffer<Instance> instances;
[[vk::binding(2, 0)]] StructuredBuffer<FrameInfo> frames;
[[vk::binding(3, 0)]] Texture2D<float4> atlas;  // sRGB, so samples are linear
[[vk::binding(4, 0)]] SamplerState atlas_sampler;

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : UV;
    nointerpolation float4 tint : TINT;  // linear, straight alpha
};

static const float2 kCorners[6] = { float2(0, 0), float2(1, 0), float2(1, 1),
                                    float2(0, 0), float2(1, 1), float2(0, 1) };

float3 srgb_to_linear(float3 c) {
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

float4 unpack_color(uint c) {
    const float4 v = float4(float(c & 255u), float((c >> 8) & 255u), float((c >> 16) & 255u), float(c >> 24)) / 255.0;
    return float4(srgb_to_linear(v.rgb), v.a);
}

// The corner's offset from the pivot in frame pixels (y down), and its atlas uv.
void corner(Instance s, FrameInfo f, uint vid, out float2 offset, out float2 uv) {
    const float2 c = kCorners[vid];
    offset = (c - f.pivot) * f.size;
    const float u = (s.flags & 1u) != 0u ? 1.0 - c.x : c.x;
    uv = lerp(f.uv.xy, f.uv.zw, float2(u, c.y));
}

[shader("vertex")]
VSOut vs_world(uint vid: SV_VertexID, uint iid: SV_InstanceID) {
    const Instance s = instances[iid];
    const FrameInfo f = frames[s.frame];
    float2 offset;
    VSOut o;
    corner(s, f, vid, offset, o.uv);
    offset *= s.scale;
    const float3 world = s.position + view.right.xyz * offset.x - float3(0.0, offset.y, 0.0);
    o.pos = mul(view.view_proj, float4(world, 1.0));
    o.tint = unpack_color(s.color);
    return o;
}

[shader("vertex")]
VSOut vs_screen(uint vid: SV_VertexID, uint iid: SV_InstanceID) {
    const Instance s = instances[iid];
    const FrameInfo f = frames[s.frame];
    float2 offset;
    VSOut o;
    corner(s, f, vid, offset, o.uv);
    const float2 p = s.position.xy + offset * s.scale;
    o.pos = float4(p.x * 2.0 * view.screen.z - 1.0, 1.0 - p.y * 2.0 * view.screen.w, 0.0, 1.0);
    o.tint = unpack_color(s.color);
    return o;
}

[shader("fragment")]
float4 fs_cutout(VSOut i) : SV_Target {
    const float4 t = atlas.Sample(atlas_sampler, i.uv) * i.tint;
    if (t.a < 0.5) discard;
    return float4(t.rgb, 1.0);
}

[shader("fragment")]
float4 fs_blend(VSOut i) : SV_Target {
    const float4 t = atlas.Sample(atlas_sampler, i.uv) * i.tint;
    return float4(t.rgb * t.a, t.a);
}
