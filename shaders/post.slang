// Post-processing of the HDR frame (render/post_renderer.cpp, D-030): a bloom chain over half-size
// levels, then the tonemap into the frame with a full-screen flash. Every pass is one full-screen
// triangle from SV_VertexID. `Post` mirrors render::PostConstants (64 bytes).

struct Post {
    float4 texel;  // 1 / width, 1 / height of `source`; zw: the same for `bloom`
    float4 bloom;  // threshold, knee, intensity, radius
    float4 flash;  // linear rgb, amount
    float4 tone;   // exposure
};

[[vk::binding(0, 0)]] ConstantBuffer<Post> post;
[[vk::binding(1, 0)]] Texture2D<float4> source;
[[vk::binding(2, 0)]] SamplerState linear_clamp;
[[vk::binding(3, 0)]] Texture2D<float4> bloom;  // composite only: bloom level 0

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : UV;
};

[shader("vertex")]
VSOut vs_fullscreen(uint vid: SV_VertexID) {
    VSOut o;
    o.uv = float2(float((vid << 1) & 2u), float(vid & 2u));
    o.pos = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.0, 1.0);
    return o;
}

float3 tap(Texture2D<float4> t, float2 uv) { return t.SampleLevel(linear_clamp, uv, 0.0).rgb; }

float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// 13 taps around uv, as five overlapping 2x2 boxes (Jimenez 2014). `karis` weights each box by
// 1 / (1 + luma), so one very bright pixel cannot flicker the whole glow.
float3 downsample(Texture2D<float4> t, float2 uv, float2 tx, bool karis) {
    const float3 a = tap(t, uv + tx * float2(-2, -2)), b = tap(t, uv + tx * float2(0, -2)), c = tap(t, uv + tx * float2(2, -2));
    const float3 d = tap(t, uv + tx * float2(-2, 0)), e = tap(t, uv), f = tap(t, uv + tx * float2(2, 0));
    const float3 g = tap(t, uv + tx * float2(-2, 2)), h = tap(t, uv + tx * float2(0, 2)), i = tap(t, uv + tx * float2(2, 2));
    const float3 j = tap(t, uv + tx * float2(-1, -1)), k = tap(t, uv + tx * float2(1, -1));
    const float3 l = tap(t, uv + tx * float2(-1, 1)), m = tap(t, uv + tx * float2(1, 1));
    float3 boxes[5] = { (j + k + l + m) * 0.25, (a + b + d + e) * 0.25, (b + c + e + f) * 0.25,
                        (d + e + g + h) * 0.25, (e + f + h + i) * 0.25 };
    const float weights[5] = { 0.5, 0.125, 0.125, 0.125, 0.125 };
    float3 sum = float3(0.0);
    float total = 0.0;
    for (int n = 0; n < 5; ++n) {
        const float w = weights[n] * (karis ? 1.0 / (1.0 + luma(boxes[n])) : 1.0);
        sum += boxes[n] * w;
        total += w;
    }
    return sum / total;
}

// 3x3 tent around uv.
float3 upsample(Texture2D<float4> t, float2 uv, float2 tx) {
    float3 s = tap(t, uv) * 4.0;
    s += (tap(t, uv + float2(-tx.x, 0)) + tap(t, uv + float2(tx.x, 0)) + tap(t, uv + float2(0, -tx.y)) +
          tap(t, uv + float2(0, tx.y))) * 2.0;
    s += tap(t, uv + float2(-tx.x, -tx.y)) + tap(t, uv + float2(tx.x, -tx.y)) + tap(t, uv + float2(-tx.x, tx.y)) +
         tap(t, uv + float2(tx.x, tx.y));
    return s / 16.0;
}

// Keeps the light above the threshold, fading in over the knee below it.
float3 threshold(float3 c) {
    const float t = post.bloom.x;
    const float knee = max(t * post.bloom.y, 1e-4);
    const float b = max(max(c.r, c.g), c.b);
    float soft = clamp(b - t + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    return c * (max(soft, b - t) / max(b, 1e-4));
}

[shader("fragment")]
float4 fs_prefilter(VSOut i) : SV_Target {
    const float3 c = downsample(source, i.uv, post.texel.xy, true) * post.tone.x;
    return float4(min(threshold(c), float3(64.0)), 1.0);
}

[shader("fragment")]
float4 fs_down(VSOut i) : SV_Target {
    return float4(downsample(source, i.uv, post.texel.xy, false), 1.0);
}

// Blended as premultiplied alpha into the level above: dst = lerp(dst, up, radius).
[shader("fragment")]
float4 fs_up(VSOut i) : SV_Target {
    const float r = post.bloom.w;
    return float4(upsample(source, i.uv, post.texel.xy) * r, r);
}

// Identity up to the shoulder, then a smooth roll-off toward 1 on the brightest channel (keeping the
// hue), and light far above 1 bleaches toward white, so bright cores of coloured light turn white.
float3 tonemap(float3 c) {
    const float shoulder = 0.8;
    const float m = max(max(c.r, c.g), c.b);
    if (m <= shoulder) return c;
    const float mapped = shoulder + (1.0 - shoulder) * (1.0 - exp(-(m - shoulder) / (1.0 - shoulder)));
    const float3 hue = c * (mapped / m);
    const float white = saturate((m - 1.0) / 6.0);
    return lerp(hue, float3(mapped), white);
}

[shader("fragment")]
float4 fs_composite(VSOut i) : SV_Target {
    float3 c = tap(source, i.uv) * post.tone.x;
    c += upsample(bloom, i.uv, post.texel.zw) * post.bloom.z;
    c = tonemap(c);
    c = saturate(c + post.flash.rgb * post.flash.w);
    // Dither by about half an 8-bit step (in a gamma-2 space close to sRGB): smooth glows on dark
    // backgrounds would show bands otherwise.
    uint h = uint(i.pos.x) * 1973u + uint(i.pos.y) * 9277u;
    h = (h ^ (h >> 13)) * 0x5bd1e995u;
    const float n = float(h & 1023u) / 1023.0 - 0.5;
    const float3 s = sqrt(c) + n / 255.0;
    return float4(max(s, float3(0.0)) * max(s, float3(0.0)), 1.0);
}
