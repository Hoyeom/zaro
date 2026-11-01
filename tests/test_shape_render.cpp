// Shapes on the GPU: every kind on the screen (filled, outlined, additive and alpha-blended, one
// with a halo) and one flat on the ground of a 3D view, checked at a few pixels. Needs a GPU (label
// `gpu`). argv[1] is the build directory (holding shaders/), argv[2] the PNG to write.
#include "core/math.h"
#include "core/png.h"
#include "render/shape_renderer.h"
#include "rhi/rhi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace zaro;

namespace {

constexpr uint32_t kWidth = 320, kHeight = 240;

int g_failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

const uint8_t* pixel(const uint8_t* rgba, uint32_t x, uint32_t y) { return rgba + ((size_t)y * kWidth + x) * 4; }

bool near(const uint8_t* p, int r, int g, int b) {
    return abs(p[0] - r) < 40 && abs(p[1] - g) < 40 && abs(p[2] - b) < 40;
}
bool is_background(const uint8_t* p) { return p[0] < 6 && p[1] < 6 && p[2] < 6; }

render::ShapeInstance screen_shape(uint32_t kind, float x, float y, float w, float h, uint32_t color) {
    render::ShapeInstance s = {};
    s.position[0] = x;
    s.position[1] = y;
    s.size[0] = w;
    s.size[1] = h;
    s.kind = kind;
    s.sides = 3;
    s.flags = render::kShapeAdditive | render::kShapeScreen;
    s.glow_strength = 0.6f;
    render::shape_color(color, 1.0f, s.color);
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: test_shape_render <build dir> <out.png>\n");
        return 2;
    }
    rhi::DeviceDesc dd;
    dd.width = kWidth;
    dd.height = kHeight;
    rhi::Device* device = rhi::create_device(dd);
    if (!device) return 1;
    char dir[600];
    snprintf(dir, sizeof(dir), "%sshaders/", argv[1]);
    render::ShapeRenderer* r = render::create_shape_renderer(device, dir);
    if (!r) return 1;

    render::ShapeInstance shapes[7];
    // World first: a magenta disc of 2 m at the origin, seen from (0, 10, 10): the middle of the frame.
    shapes[0] = screen_shape(render::kShapeCircle, 0.0f, 0.0f, 2.0f, 2.0f, 0xffff00ffu);
    shapes[0].flags = render::kShapeAdditive;
    shapes[1] = screen_shape(render::kShapeCircle, 60.0f, 60.0f, 40.0f, 40.0f, 0xffffffffu);
    shapes[2] = screen_shape(render::kShapeRing, 160.0f, 60.0f, 60.0f, 60.0f, 0xff0000ffu);
    shapes[2].thickness = 6.0f;
    shapes[3] = screen_shape(render::kShapeRect, 260.0f, 60.0f, 60.0f, 40.0f, 0xff00ff00u);
    shapes[3].thickness = 4.0f;
    shapes[3].flags = render::kShapeScreen;  // alpha-blended
    // A triangle with a corner on +x: it reaches 30 px right of its centre but only 15 px left.
    shapes[4] = screen_shape(render::kShapePolygon, 60.0f, 170.0f, 60.0f, 60.0f, 0xffff0000u);
    shapes[5] = screen_shape(render::kShapeLine, 160.0f, 170.0f, 80.0f, 0.0f, 0xff00ffffu);
    shapes[5].thickness = 6.0f;
    shapes[6] = screen_shape(render::kShapeCircle, 260.0f, 170.0f, 30.0f, 30.0f, 0xffffffffu);
    shapes[6].glow = 20.0f;

    const Mat4 vp = mul(perspective(0.87f, (float)kWidth / (float)kHeight, 0.5f, 100.0f),
                        look_at(Vec3{0, 10, 10}, Vec3{0, 0, 0}, Vec3{0, 1, 0}));
    render::ShapeDraw draw;
    draw.instances = shapes;
    draw.world_count = 1;
    draw.screen_count = 6;
    memcpy(draw.view_proj, vp.m, sizeof(draw.view_proj));

    if (!rhi::begin_frame(device)) return 1;
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    rhi::render_begin(device, "clear", clear, rhi::kDepthStore);
    rhi::render_end(device);
    render::render_shapes(r, draw);
    rhi::end_frame(device);
    rhi::wait_idle(device);

    uint8_t* px = (uint8_t*)malloc((size_t)kWidth * kHeight * 4);
    expect(rhi::capture_rgba8(device, px), "capture");
    expect(write_png_rgba8(argv[2], kWidth, kHeight, px), "write PNG");
    expect(!rhi::device_failed(device), "no GPU errors");
    expect(rhi::last_draw_calls(device) == 2, "one draw call for world shapes, one for screen shapes");
    expect(near(pixel(px, 160, 120), 255, 0, 255), "world disc at the middle of the frame");
    expect(is_background(pixel(px, 160, 150)), "nothing below the world disc");
    expect(near(pixel(px, 60, 60), 255, 255, 255), "filled circle covers its centre");
    expect(is_background(pixel(px, 90, 60)), "no halo without glow");
    expect(is_background(pixel(px, 160, 60)), "a ring is hollow");
    expect(near(pixel(px, 187, 60), 255, 0, 0), "a ring's outline");
    expect(is_background(pixel(px, 260, 60)), "an outlined rect is hollow");
    expect(near(pixel(px, 288, 60), 0, 255, 0), "an outlined rect's side");
    expect(near(pixel(px, 80, 170), 0, 0, 255), "the triangle's corner points along +x");
    expect(is_background(pixel(px, 40, 170)), "the triangle's flat side is left of its centre");
    expect(near(pixel(px, 190, 170), 255, 255, 0), "the line along +x");
    expect(is_background(pixel(px, 160, 180)), "the line is as thick as given");
    const uint8_t* halo = pixel(px, 285, 170);
    expect(halo[0] > 20 && halo[0] < 200, "a halo fades out beyond the edge");
    expect(is_background(pixel(px, 300, 170)), "the halo ends at its width");
    free(px);

    render::destroy_shape_renderer(r);
    rhi::destroy_device(device);
    if (g_failures) {
        fprintf(stderr, "%d failure(s), see %s\n", g_failures, argv[2]);
        return 1;
    }
    printf("shape render: ok (%s)\n", argv[2]);
    return 0;
}
