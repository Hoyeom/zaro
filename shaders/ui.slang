// UI quads in screen pixels: rounded rectangles with borders, and glyphs from the atlas.
// One instance per quad; the six corners come from SV_VertexID, so no vertex buffers are bound.
// `Quad` mirrors ui::Quad (src/ui/draw.h) and render::UiQuad (80 bytes); keep all three in step.

struct Quad {
    float4 rect;  // x0, y0, x1, y1 in pixels, origin top-left
    float4 uv;    // glyph or image atlas u0, v0, u1, v1
    float4 clip;  // x0, y0, x1, y1
    uint color;   // sRGB RGBA8, red in the low byte, straight alpha
    uint border_color;
    uint kind;    // 0 shape, 1 glyph, 2 image
    float radius;
    float border;
    float pad0;
    float pad1;
    float pad2;
};

struct Screen {
    float4 size;  // width, height, 1 / width, 1 / height
};

[[vk::binding(0, 0)]] ConstantBuffer<Screen> screen;
[[vk::binding(1, 0)]] StructuredBuffer<Quad> quads;
[[vk::binding(2, 0)]] Texture2D<float4> atlas;
[[vk::binding(3, 0)]] SamplerState atlas_sampler;
[[vk::binding(4, 0)]] Texture2D<float4> images;  // sRGB, so samples are linear

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : UV;
    nointerpolation uint quad : QUAD;
};

float3 srgb_to_linear(float3 c) {
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

// Unpacks an sRGB RGBA8 colour to premultiplied linear RGBA.
float4 premultiplied(uint c) {
    const float4 v = float4(float(c & 255u), float((c >> 8) & 255u), float((c >> 16) & 255u), float(c >> 24)) / 255.0;
    return float4(srgb_to_linear(v.rgb) * v.a, v.a);
}

[shader("vertex")]
VSOut vs_quad(uint vid: SV_VertexID, uint iid: SV_InstanceID) {
    static const float2 kCorners[6] = { float2(0, 0), float2(1, 0), float2(1, 1),
                                        float2(0, 0), float2(1, 1), float2(0, 1) };
    const Quad q = quads[iid];
    const float2 c = kCorners[vid];
    // Shapes grow by a pixel so their anti-aliased edge is not cut off.
    const float grow = q.kind == 0u ? 1.0 : 0.0;
    const float2 p = lerp(q.rect.xy - grow, q.rect.zw + grow, c);
    VSOut o;
    o.pos = float4(p.x * 2.0 * screen.size.z - 1.0, 1.0 - p.y * 2.0 * screen.size.w, 0.0, 1.0);
    o.uv = lerp(q.uv.xy, q.uv.zw, c);
    o.quad = iid;
    return o;
}

[shader("fragment")]
float4 fs_quad(VSOut i) : SV_Target {
    const Quad q = quads[i.quad];
    const float2 p = i.pos.xy;  // pixel centre, origin top-left
    if (p.x < q.clip.x || p.y < q.clip.y || p.x >= q.clip.z || p.y >= q.clip.w) discard;
    const float4 color = premultiplied(q.color);
    // SampleLevel: the branch on q.kind is not uniform control flow, where WGSL forbids Sample.
    if (q.kind == 1u) return color * atlas.SampleLevel(atlas_sampler, i.uv, 0.0).r;
    if (q.kind == 2u) {
        const float4 texel = images.SampleLevel(atlas_sampler, i.uv, 0.0);
        return color * float4(texel.rgb * texel.a, texel.a);
    }

    // Signed distance to the rounded rectangle; negative inside.
    const float2 half_size = (q.rect.zw - q.rect.xy) * 0.5;
    const float r = min(q.radius, min(half_size.x, half_size.y));
    const float2 d = abs(p - (q.rect.xy + q.rect.zw) * 0.5) - half_size + r;
    const float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
    const float outer = saturate(0.5 - dist);
    if (q.border <= 0.0) return color * outer;
    const float inner = saturate(0.5 - (dist + q.border));
    return premultiplied(q.border_color) * (outer - inner) + color * inner;
}
