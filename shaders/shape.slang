// Glowing shapes (render/shape_renderer.cpp): one quad per shape, shaded from a signed distance.
// World shapes lie flat on the ground plane at their height, depth-tested against the 3D view
// without writing depth; screen shapes are pixels over it. `Instance` mirrors
// render::ShapeInstance (64 bytes); keep them in step.
import shape_sdf;

struct View {
    float4x4 view_proj;  // column-major, clip = view_proj * world
    float4 screen;       // width, height, 1 / width, 1 / height
};

struct Instance {
    float3 position;  // world metres (x, height, z), or screen pixels (z unused)
    float rotation;   // radians; world: from +x toward -z, screen: clockwise
    float2 size;
    float thickness;
    float glow;           // halo width beyond the edge, in the shape's units
    float4 color;         // linear rgb times intensity, opacity
    uint kind;            // render::ShapeKind
    uint sides;           // polygon
    uint flags;           // 1: additive, 2: screen
    float glow_strength;  // halo opacity at the edge
};

[[vk::binding(0, 0)]] ConstantBuffer<View> view;
[[vk::binding(1, 0)]] StructuredBuffer<Instance> instances;

struct VSOut {
    float4 pos : SV_Position;
    float2 local : LOCAL;  // position in the shape's frame, its units
    nointerpolation uint index : INDEX;
};

static const float2 kCorners[6] = { float2(-1, -1), float2(1, -1), float2(1, 1),
                                    float2(-1, -1), float2(1, 1), float2(-1, 1) };

[shader("vertex")]
VSOut vs_shape(uint vid: SV_VertexID, uint iid: SV_InstanceID) {
    const Instance s = instances[iid];
    const bool screen = (s.flags & 2u) != 0u;
    // Room for the glow and the antialiased edge (1.5 pixels, or 6 cm in the world).
    const float margin = s.glow + (screen ? 1.5 : 0.06);
    const float2 local = kCorners[vid] * (shape_extent(s.kind, s.size, s.thickness) + margin);
    const float c = cos(s.rotation), sn = sin(s.rotation);
    const float2 r = float2(c * local.x - sn * local.y, sn * local.x + c * local.y);
    VSOut o;
    if (screen) {
        const float2 p = s.position.xy + r;
        o.pos = float4(p.x * 2.0 * view.screen.z - 1.0, 1.0 - p.y * 2.0 * view.screen.w, 0.0, 1.0);
    } else {
        // Local +y is world -z, so a shape reads the same way up as on the screen.
        o.pos = mul(view.view_proj, float4(s.position + float3(r.x, 0.0, -r.y), 1.0));
    }
    o.local = local;
    o.index = iid;
    return o;
}

[shader("fragment")]
float4 fs_shape(VSOut i) : SV_Target {
    const Instance s = instances[i.index];
    const float d = shape_distance(s.kind, i.local, s.size, s.thickness, float(s.sides));
    return shape_shade(d, fwidth(d), s.glow, s.glow_strength, s.color, (s.flags & 1u) != 0u);
}
