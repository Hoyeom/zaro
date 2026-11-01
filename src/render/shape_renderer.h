// Shape renderer: glowing rectangles, circles, rings, regular polygons and lines, each one quad
// shaded from a signed distance (shaders/shape_sdf.slang), with an antialiased edge and a halo.
// World shapes lie flat on the ground plane of the 3D view, depth-tested against it without
// writing depth; screen shapes are pixels over it. Additive shapes add light (the neon look,
// D-029); others blend by alpha. It draws plain data; the app turns Shape components into
// instances. All shapes are one draw call per space.
#pragma once

#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro::render {

enum ShapeKind : uint32_t { kShapeRect, kShapeCircle, kShapeRing, kShapePolygon, kShapeLine };
enum ShapeFlags : uint32_t { kShapeAdditive = 1u << 0, kShapeScreen = 1u << 1 };

// 64 bytes. Mirrors `struct Instance` in shaders/shape.slang.
struct ShapeInstance {
    float position[3];  // world metres (x, height, z), or screen pixels from the top-left (z unused)
    float rotation;     // radians; world: from +x toward -z, screen: clockwise
    float size[2];      // full width and height (rect); diameter in x (circle, ring, polygon); length (line)
    float thickness;    // outline width, 0 fills; a line's width
    float glow;         // halo width beyond the edge, in the shape's units
    float color[4];     // linear rgb times intensity, opacity
    uint32_t kind;      // ShapeKind
    uint32_t sides;     // polygon corners, 3 or more
    uint32_t flags;     // ShapeFlags
    float glow_strength;  // halo opacity at the edge, 0..1
};
static_assert(sizeof(ShapeInstance) == 64, "must match shaders/shape.slang");

struct ShapeDraw {
    const ShapeInstance* instances = nullptr;  // world shapes first, then screen shapes
    uint32_t world_count = 0;
    uint32_t screen_count = 0;
    float view_proj[16] = {};  // the 3D view's camera (render::crowd_camera)
    rhi::Texture* target = nullptr;  // nullptr draws into the frame; else the 3D view's HDR target
    bool keep_depth = false;  // a later pass loads the depth buffer
};

struct ShapeRenderer;

// shader_dir holds shape.wgsl. `format` is the format of ShapeDraw::target.
ShapeRenderer* create_shape_renderer(rhi::Device* device, const char* shader_dir,
                                     rhi::TextureFormat format = rhi::TextureFormat::kFrame);
void destroy_shape_renderer(ShapeRenderer* renderer);
bool reload_shape_shaders(ShapeRenderer* renderer);

// Records the "shapes" pass over what earlier passes drew. It loads their depth, so the 3D view's
// pass must keep it (CrowdScene::keep_depth). Call between rhi::begin_frame and rhi::end_frame.
void render_shapes(ShapeRenderer* renderer, const ShapeDraw& draw);

// Linear colour of an sRGB RGBA8 colour (red in the low byte) times intensity, alpha unchanged.
void shape_color(uint32_t srgb_rgba8, float intensity, float out[4]);

}  // namespace zaro::render
