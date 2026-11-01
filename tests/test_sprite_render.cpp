// Sprites on the GPU: a billboard in the 3D view (depth-tested against a pass before it, mirrored)
// and a screen sprite, from tests/data/sprites/blob.png. The frame is written as PNG and checked at
// a few pixels. Needs a GPU (label `gpu`). argv[1] is the build directory (holding shaders/), argv[2]
// the PNG to write. Runs from the repository root.
#include "core/math.h"
#include "core/png.h"
#include "render/sprite_renderer.h"
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

bool is_green(const uint8_t* p) { return p[1] > 150 && p[0] < 120 && p[2] < 140; }
bool is_red(const uint8_t* p) { return p[0] > 40 && p[1] < 20 && p[2] < 20; }
bool is_background(const uint8_t* p) { return p[0] < 10 && p[1] < 10 && p[2] > 50 && p[2] < 80; }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: test_sprite_render <build dir> <out.png>\n");
        return 2;
    }
    uint8_t* sheet = nullptr;
    uint32_t sw = 0, sh = 0;
    if (!read_png_rgba8("tests/data/sprites/blob.png", &sheet, &sw, &sh) || sw != 128 || sh != 32) {
        fprintf(stderr, "FAIL: cannot read tests/data/sprites/blob.png\n");
        return 1;
    }
    rhi::DeviceDesc dd;
    dd.width = kWidth;
    dd.height = kHeight;
    rhi::Device* device = rhi::create_device(dd);
    if (!device) return 1;
    char dir[600];
    snprintf(dir, sizeof(dir), "%sshaders/", argv[1]);
    render::SpriteRenderer* r = render::create_sprite_renderer(device, dir);
    if (!r) return 1;

    render::SpriteFrameInfo frames[4];
    for (uint32_t i = 0; i < 4; ++i) {
        frames[i] = {{i * 0.25f, 0.0f, (i + 1) * 0.25f, 1.0f}, {32.0f, 32.0f}, {0.5f, 1.0f}};
    }
    const uint32_t blob = render::add_sprite_sheet(r, sheet, sw, sh, frames, 4, "blob");
    free_png(sheet);

    // Camera 10 m in front of the origin at 1 m height, looking along -z: at 10 m, 1 m covers about 26
    // pixels and y = 1 is the middle row.
    const Mat4 vp = mul(perspective(0.87f, (float)kWidth / (float)kHeight, 0.5f, 100.0f),
                        look_at(Vec3{0, 1, 10}, Vec3{0, 1, 0}, Vec3{0, 1, 0}));
    render::SpriteInstance inst[3] = {};
    // World: frame 0 at the origin, 2 m tall (scale = metres per pixel), mirrored.
    inst[0] = {{0.0f, 0.0f, 0.0f}, 2.0f / 32.0f, 0, 0xffffffffu, render::kSpriteFlipX, 0.0f};
    // World, tinted red, 5 m behind the first and drawn after it: hidden where they overlap.
    inst[1] = {{1.5f, 0.0f, -5.0f}, 2.0f / 32.0f, 1, 0xff0000ffu, 0, 0.0f};
    // Screen: frame 2 at twice its size, its bottom centre at (260, 220).
    inst[2] = {{260.0f, 220.0f, 0.0f}, 2.0f, 2, 0xffffffffu, 0, 0.0f};
    const render::SpriteBatch batches[2] = {{blob, 0, 2, false}, {blob, 2, 1, true}};
    render::SpriteDraw draw;
    draw.instances = inst;
    draw.instance_count = 3;
    draw.batches = batches;
    draw.batch_count = 2;
    memcpy(draw.view_proj, vp.m, sizeof(draw.view_proj));

    if (!rhi::begin_frame(device)) return 1;
    const float clear[4] = {0.0f, 0.0f, 0.05f, 1.0f};  // sRGB 0, 0, 64 after encoding
    rhi::render_begin(device, "clear", clear, rhi::kDepthStore);
    rhi::render_end(device);
    render::render_sprites(r, draw);
    rhi::end_frame(device);
    rhi::wait_idle(device);

    uint8_t* px = (uint8_t*)malloc((size_t)kWidth * kHeight * 4);
    expect(rhi::capture_rgba8(device, px), "capture");
    expect(write_png_rgba8(argv[2], kWidth, kHeight, px), "write PNG");
    expect(!rhi::device_failed(device), "no GPU errors");
    expect(rhi::last_draw_calls(device) == 2, "one draw call per batch");
    // The first billboard spans x 134..186, y 94..146; its body starts about 10 pixels below the top.
    expect(is_green(pixel(px, 160, 125)), "world sprite drawn at the origin");
    expect(is_background(pixel(px, 160, 60)), "nothing above the world sprite");
    expect(is_background(pixel(px, 137, 97)), "transparent texels are cut out");
    // The red one behind it spans x 169..203, y 103..137.
    expect(is_red(pixel(px, 195, 125)), "the far sprite shows beside the near one");
    expect(is_green(pixel(px, 172, 125)), "the near sprite hides the far one (depth test)");
    // The screen sprite covers x 228..292, y 156..220.
    expect(is_green(pixel(px, 260, 200)), "screen sprite drawn at its pixels");
    expect(is_background(pixel(px, 260, 150)), "nothing above the screen sprite");
    free(px);

    render::destroy_sprite_renderer(r);
    rhi::destroy_device(device);
    if (g_failures) {
        fprintf(stderr, "%d failure(s), see %s\n", g_failures, argv[2]);
        return 1;
    }
    printf("sprite render: ok (%s)\n", argv[2]);
    return 0;
}
