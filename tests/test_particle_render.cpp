// GPU particles: one burst of sparks flying along +x, emitted on the first frame and moved on the
// second, checked at the pixels where they should and should not be. Needs a GPU (label `gpu`).
// argv[1] is the build directory (holding shaders/), argv[2] the PNG to write.
#include "core/math.h"
#include "core/png.h"
#include "render/particle_renderer.h"
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

// The pixel a world point lands on.
void project(const Mat4& vp, Vec3 p, uint32_t* x, uint32_t* y) {
    const float* m = vp.m;  // column-major
    const float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    const float cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    const float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    *x = (uint32_t)((cx / cw * 0.5f + 0.5f) * kWidth);
    *y = (uint32_t)((0.5f - cy / cw * 0.5f) * kHeight);
}

// Brightest pixel in a 7x7 box around (x, y), summed over its channels.
int brightest(const uint8_t* px, uint32_t x, uint32_t y) {
    int best = 0;
    for (uint32_t j = y - 3; j <= y + 3; ++j) {
        for (uint32_t i = x - 3; i <= x + 3; ++i) {
            const uint8_t* p = pixel(px, i, j);
            const int s = p[0] + p[1] + p[2];
            if (s > best) best = s;
        }
    }
    return best;
}

bool draw_frame(rhi::Device* device, render::ParticleRenderer* r, const render::ParticleFrame& f, uint8_t* out,
                float clear_red = 0.0f) {
    if (!rhi::begin_frame(device)) return false;
    const float clear[4] = {clear_red, 0.0f, 0.0f, 1.0f};
    rhi::render_begin(device, "clear", clear);
    rhi::render_end(device);
    render::render_particles(r, f);
    rhi::end_frame(device);
    rhi::wait_idle(device);
    return rhi::capture_rgba8(device, out);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: test_particle_render <build dir> <out.png>\n");
        return 2;
    }
    rhi::DeviceDesc dd;
    dd.width = kWidth;
    dd.height = kHeight;
    rhi::Device* device = rhi::create_device(dd);
    if (!device) return 1;
    char dir[600];
    snprintf(dir, sizeof(dir), "%sshaders/", argv[1]);
    render::ParticleRenderer* r = render::create_particle_renderer(device, dir, 1024);
    if (!r) return 1;

    const Vec3 eye{0, 10, 10};
    const Mat4 vp = mul(perspective(0.87f, (float)kWidth / (float)kHeight, 0.5f, 100.0f),
                        look_at(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0}));
    const Vec3 fwd = normalize(Vec3{0, 0, 0} - eye);
    render::ParticleBurst burst = {};
    burst.count = 200;
    burst.color[0] = burst.color[1] = burst.color[2] = burst.color[3] = 1.0f;
    burst.speed[0] = burst.speed[1] = 10.0f;  // every spark at 10 m/s along +x
    burst.life = 5.0f;
    burst.size = 0.3f;
    burst.seed = 7;
    render::ParticleFrame f;
    f.bursts = &burst;
    f.burst_count = 1;
    f.seconds = 0.0f;
    memcpy(f.view_proj, vp.m, sizeof(f.view_proj));
    f.camera_forward[0] = fwd.x;
    f.camera_forward[1] = fwd.y;
    f.camera_forward[2] = fwd.z;

    uint8_t* first = (uint8_t*)malloc((size_t)kWidth * kHeight * 4);
    uint8_t* second = (uint8_t*)malloc((size_t)kWidth * kHeight * 4);
    expect(draw_frame(device, r, f, first), "capture the first frame");
    f.burst_count = 0;
    f.seconds = 0.2f;  // 2 m along +x
    expect(draw_frame(device, r, f, second), "capture the second frame");
    expect(write_png_rgba8(argv[2], kWidth, kHeight, second), "write PNG");
    expect(!rhi::device_failed(device), "no GPU errors");
    expect(render::particles_emitted(r) == 200, "200 particles emitted");

    uint32_t ox, oy, mx, my, bx, by;
    project(vp, Vec3{0, 0, 0}, &ox, &oy);
    project(vp, Vec3{2, 0, 0}, &mx, &my);
    project(vp, Vec3{-2, 0, 0}, &bx, &by);
    expect(brightest(first, ox, oy) > 300, "new sparks start at their burst");
    expect(brightest(first, mx, my) == 0, "nothing ahead of them yet");
    expect(brightest(second, mx, my) > 300, "a frame later they are 2 m along +x");
    expect(brightest(second, ox, oy) < 60, "and gone from the burst");
    expect(brightest(second, bx, by) == 0, "none flew the other way");
    render::destroy_particle_renderer(r);

    // One motionless blue particle of each shape over a red frame (D-040): a solid piece covers the
    // red, a spark only adds to it. A piece is 0.7..1.3 m across at first (size 2 m), so a point
    // 0.6 m from its centre is inside it at any angle; at 2.9 s of a life of at least 3 s, it is out.
    burst.count = 1;
    burst.color[0] = burst.color[1] = 0.0f;
    burst.speed[0] = burst.speed[1] = 0.0f;
    burst.size = 2.0f;
    uint32_t ix, iy, fx, fy;
    project(vp, Vec3{0.6f, 0, 0}, &ix, &iy);
    project(vp, Vec3{3, 0, 0}, &fx, &fy);
    for (uint32_t shape = 0; shape < 4; ++shape) {
        r = render::create_particle_renderer(device, dir, 16);
        if (!r) return 1;
        burst.shape = shape;
        f.burst_count = 1;
        f.seconds = 0.0f;
        expect(draw_frame(device, r, f, first, 1.0f), "capture a shape");
        const uint8_t* centre = pixel(first, ox, oy);
        expect(centre[2] > 200, "the particle shows at its burst");
        if (shape) {
            expect(centre[0] < 30, "a solid piece covers what is behind it");
        } else {
            expect(centre[0] > 200, "a spark adds to what is behind it");
        }
        expect(pixel(first, fx, fy)[2] == 0 && pixel(first, fx, fy)[0] > 200, "nothing far from the particle");
        if (shape == 1) {
            expect(pixel(first, ix, iy)[0] < 30, "a square reaches 0.6 m at first");
            f.burst_count = 0;
            f.seconds = 2.9f;
            expect(draw_frame(device, r, f, second, 1.0f), "capture the shrunk piece");
            expect(pixel(second, ix, iy)[0] > 200, "and shrinks away from it with age");
        }
        render::destroy_particle_renderer(r);
    }
    expect(!rhi::device_failed(device), "no GPU errors with solid pieces");
    free(first);
    free(second);

    rhi::destroy_device(device);
    if (g_failures) {
        fprintf(stderr, "%d failure(s), see %s\n", g_failures, argv[2]);
        return 1;
    }
    printf("particle render: ok (%s)\n", argv[2]);
    return 0;
}
