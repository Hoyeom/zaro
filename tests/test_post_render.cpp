// The HDR frame and bloom on the GPU (D-030): a green screen sprite is drawn into the HDR target,
// then tonemapped into the frame without and with bloom, for each HDR format the GPU renders. The
// glow must light pixels beside the sprite and leave far ones alone, and without bloom the colours
// pass through unchanged. Needs a GPU (label `gpu`). argv[1] is the build directory (holding
// shaders/), argv[2] the PNG to write (the last frame with bloom). Runs from the repository root.
#include "core/png.h"
#include "render/post_renderer.h"
#include "render/sprite_renderer.h"
#include "rhi/rhi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace zaro;

namespace {

constexpr uint32_t kWidth = 320, kHeight = 240;

int g_failures = 0;

void expect(bool ok, const char* what, const char* format) {
    if (!ok) {
        fprintf(stderr, "FAIL (%s): %s\n", format, what);
        ++g_failures;
    }
}

const uint8_t* pixel(const uint8_t* rgba, uint32_t x, uint32_t y) { return rgba + ((size_t)y * kWidth + x) * 4; }

int sum(const uint8_t* p) { return p[0] + p[1] + p[2]; }

// Draws the sprite into the HDR target and tonemaps it into the frame; returns the captured frame.
bool draw_frame(rhi::Device* device, render::PostRenderer* post, render::SpriteRenderer* sprites,
                render::SpriteDraw draw, const render::PostSettings& settings, uint8_t* out) {
    if (!rhi::begin_frame(device)) return false;
    draw.target = render::post_scene_target(post, settings);
    const float clear[4] = {0.0f, 0.0f, 0.05f, 1.0f};  // sRGB 0, 0, 64 after encoding
    rhi::render_begin(device, "clear", clear, rhi::kDepthStore, draw.target);
    rhi::render_end(device);
    render::render_sprites(sprites, draw);
    render::render_post(post, settings);
    rhi::end_frame(device);
    rhi::wait_idle(device);
    return rhi::capture_rgba8(device, out);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: test_post_render <build dir> <out.png>\n");
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

    const rhi::TextureFormat formats[2] = {rhi::TextureFormat::kRGBA16Float, rhi::TextureFormat::kRG11B10Float};
    const char* names[2] = {"rgba16f", "rg11b10f"};
    uint8_t* plain = (uint8_t*)malloc((size_t)kWidth * kHeight * 4);
    uint8_t* glow = (uint8_t*)malloc((size_t)kWidth * kHeight * 4);
    uint32_t tested = 0;
    for (int f = 0; f < 2; ++f) {
        if (!rhi::supports_format(device, formats[f])) {
            printf("post render: %s is not renderable on this GPU, skipped\n", names[f]);
            continue;
        }
        render::PostRenderer* post = render::create_post_renderer(device, dir, formats[f]);
        render::SpriteRenderer* sprites = render::create_sprite_renderer(device, dir, formats[f]);
        if (!post || !sprites) return 1;
        render::SpriteFrameInfo frame = {{0.0f, 0.0f, 0.25f, 1.0f}, {32.0f, 32.0f}, {0.5f, 1.0f}};
        const uint32_t blob = render::add_sprite_sheet(sprites, sheet, sw, sh, &frame, 1, "blob");
        // Frame 0 at twice its size, its bottom centre at (160, 150): x 128..192, y 86..150.
        const render::SpriteInstance inst = {{160.0f, 150.0f, 0.0f}, 2.0f, 0, 0xffffffffu, 0, 0.0f};
        const render::SpriteBatch batch = {blob, 0, 1, true};
        render::SpriteDraw draw;
        draw.instances = &inst;
        draw.instance_count = 1;
        draw.batches = &batch;
        draw.batch_count = 1;

        render::PostSettings off;
        off.bloom_intensity = 0.0f;
        render::PostSettings on;
        on.bloom_threshold = 0.3f;
        on.bloom_intensity = 2.0f;
        expect(draw_frame(device, post, sprites, draw, off, plain), "capture without bloom", names[f]);
        expect(draw_frame(device, post, sprites, draw, on, glow), "capture with bloom", names[f]);
        expect(!rhi::device_failed(device), "no GPU errors", names[f]);

        const uint8_t* bg = pixel(plain, 10, 10);
        expect(bg[0] < 4 && bg[1] < 4 && bg[2] > 58 && bg[2] < 70, "without bloom the background passes through",
               names[f]);
        const uint8_t* body = pixel(plain, 160, 125);
        expect(body[1] > 150 && body[0] < 120, "without bloom the sprite keeps its colour", names[f]);
        expect(sum(pixel(plain, 160, 76)) == sum(pixel(plain, 160, 40)), "without bloom nothing beside the sprite",
               names[f]);
        expect(pixel(glow, 160, 76)[1] > pixel(plain, 160, 76)[1] + 20, "bloom lights pixels beside the sprite",
               names[f]);
        expect(abs(sum(pixel(glow, 5, 5)) - sum(pixel(plain, 5, 5))) <= 6, "bloom leaves far pixels alone", names[f]);
        if (tested == 0 || f == 1) expect(write_png_rgba8(argv[2], kWidth, kHeight, glow), "write PNG", names[f]);
        render::destroy_sprite_renderer(sprites);
        render::destroy_post_renderer(post);
        ++tested;
    }
    free(plain);
    free(glow);
    free_png(sheet);
    rhi::destroy_device(device);
    if (!tested) fprintf(stderr, "FAIL: no HDR format is renderable\n");
    if (g_failures || !tested) {
        fprintf(stderr, "%d failure(s), see %s\n", g_failures, argv[2]);
        return 1;
    }
    printf("post render: ok, %u format(s) (%s)\n", tested, argv[2]);
    return 0;
}
