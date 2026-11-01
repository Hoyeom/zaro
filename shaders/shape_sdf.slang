// Signed distances and shading of glowing shapes, shared by shaders/shape.slang (Shape
// components) and shaders/crowd_draw.slang (CrowdShape enemies). Distances are negative inside.
module shape_sdf;

// render::ShapeKind
public static const uint kShapeRect = 0;
public static const uint kShapeCircle = 1;
public static const uint kShapeRing = 2;
public static const uint kShapePolygon = 3;
public static const uint kShapeLine = 4;

static const float kPi = 3.14159265;

float sd_box(float2 p, float2 half_size) {
    const float2 d = abs(p) - half_size;
    return length(max(d, float2(0.0))) + min(max(d.x, d.y), 0.0);
}

// Regular polygon with circumradius r and a corner on +x.
float sd_polygon(float2 p, float r, float sides) {
    const float an = 2.0 * kPi / sides;
    const float apothem = r * cos(kPi / sides);
    const float half_edge = r * sin(kPi / sides);
    // The nearest edge's middle is at angle (k + 0.5) * an; turn p so it lies on +x.
    const float bn = an * floor(atan2(p.y, p.x) / an) + 0.5 * an;
    const float c = cos(bn), s = sin(bn);
    const float2 q = float2(c * p.x + s * p.y, -s * p.x + c * p.y);
    return length(q - float2(apothem, clamp(q.y, -half_edge, half_edge))) * sign(q.x - apothem);
}

// size: full width and height (rect), diameter in x (circle, ring, polygon), length in x (line).
// thickness: outline width, 0 fills; a ring of thickness 0 gets a tenth of its diameter, a line
// is always `thickness` wide.
public float shape_distance(uint kind, float2 p, float2 size, float thickness, float sides) {
    float d;
    float t = thickness;
    if (kind == kShapeRect) {
        d = sd_box(p, size * 0.5);
    } else if (kind == kShapePolygon) {
        d = sd_polygon(p, size.x * 0.5, max(sides, 3.0));
    } else if (kind == kShapeLine) {
        const float half_len = size.x * 0.5;
        return length(p - float2(clamp(p.x, -half_len, half_len), 0.0)) - thickness * 0.5;
    } else {
        d = length(p) - size.x * 0.5;
        if (kind == kShapeRing && t <= 0.0) t = size.x * 0.1;
    }
    return t > 0.0 ? abs(d + t * 0.5) - t * 0.5 : d;
}

// How far the shape reaches from its centre along each local axis (before the glow).
public float2 shape_extent(uint kind, float2 size, float thickness) {
    if (kind == kShapeRect) return size * 0.5;
    if (kind == kShapeLine) return float2(size.x * 0.5 + thickness * 0.5, thickness * 0.5);
    return float2(size.x * 0.5);
}

// Premultiplied colour of a fragment `d` from the edge: the shape's coverage (antialiased over
// `aa`), and outside it a halo that fades over `glow` with `strength`. `color` is linear RGB times
// intensity, a its opacity. Additive shapes return alpha 0, so blending adds their light.
public float4 shape_shade(float d, float aa, float glow, float strength, float4 color, bool additive) {
    const float cover = saturate(0.5 - d / max(aa, 1e-5));
    float halo = 0.0;
    if (glow > 0.0) {
        const float t = saturate(1.0 - max(d, 0.0) / glow);
        halo = t * t * strength;
    }
    const float a = (cover + (1.0 - cover) * halo) * color.a;
    return float4(color.rgb * a, additive ? 0.0 : a);
}
