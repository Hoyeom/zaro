// The scene camera's projection (D-035), without a GPU: render::crowd_camera for a CrowdCamera
// against the default view, a portrait screen keeping its width, and a camera looking straight down.
#include "render/crowd_renderer.h"

#include <math.h>
#include <stdio.h>

using namespace zaro;

namespace {

int g_failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

// Normalized device x and y of a world point.
void project(const float vp[16], float x, float y, float z, float* nx, float* ny) {
    const float cx = vp[0] * x + vp[4] * y + vp[8] * z + vp[12];
    const float cy = vp[1] * x + vp[5] * y + vp[9] * z + vp[13];
    const float cw = vp[3] * x + vp[7] * y + vp[11] * z + vp[15];
    *nx = cx / cw;
    *ny = cy / cw;
}

float radians(float degrees) { return degrees * 3.14159265f / 180.0f; }

}  // namespace

int main() {
    render::CrowdScene scene;
    scene.player_x = 40.0f;
    scene.player_z = 60.0f;
    float vp[16], right[3];

    // Without a camera: 30 m up, 24 m back, 0.87 rad vertical. The Camera defaults match it.
    float legacy[16];
    render::crowd_camera(scene, 1920, 1080, legacy, right);
    render::CrowdCamera cam;
    cam.target[0] = 40.0f;
    cam.target[2] = 60.0f;
    cam.height = 30.0f;
    cam.pitch = radians(51.34f);
    cam.fov = radians(49.85f);
    scene.camera = &cam;
    render::crowd_camera(scene, 1920, 1080, vp, right);
    float worst = 0.0f;  // where ground points around the player land on the screen
    for (float dz = -20.0f; dz <= 20.0f; dz += 5.0f) {
        for (float dx = -30.0f; dx <= 30.0f; dx += 5.0f) {
            float ax, ay, bx, by;
            project(vp, 40.0f + dx, 0.0f, 60.0f + dz, &ax, &ay);
            project(legacy, 40.0f + dx, 0.0f, 60.0f + dz, &bx, &by);
            worst = fmaxf(worst, fmaxf(fabsf(ax - bx), fabsf(ay - by)));
        }
    }
    printf("largest difference to the default view: %g\n", worst);
    expect(worst < 2e-3f, "Camera defaults give the default view");
    expect(right[0] == 1.0f && right[1] == 0.0f && right[2] == 0.0f, "the camera's right is +x");

    // Looking straight down: finite, the target in the centre, -z up on the screen, +x to the right.
    cam.pitch = radians(90.0f);
    cam.fov = radians(60.0f);
    cam.fov_axis = 2;  // the shorter side
    render::crowd_camera(scene, 1080, 1920, vp, right);
    float nx, ny;
    project(vp, 40.0f, 0.0f, 60.0f, &nx, &ny);
    expect(fabsf(nx) < 1e-4f && fabsf(ny) < 1e-4f, "the target is in the centre of the screen");
    project(vp, 40.0f, 0.0f, 50.0f, &nx, &ny);
    expect(ny > 0.1f && fabsf(nx) < 1e-4f, "-z is up on the screen from straight above");

    // fov across the shorter side: on a portrait screen it spans the width, so a point at the edge of
    // the field of view lands on the screen's edge, as the same point does vertically on landscape.
    const float edge = 30.0f * tanf(radians(30.0f));
    project(vp, 40.0f + edge, 0.0f, 60.0f, &nx, &ny);
    expect(fabsf(nx - 1.0f) < 1e-3f, "portrait, shorter side: fov spans the width");
    render::crowd_camera(scene, 1920, 1080, vp, right);
    project(vp, 40.0f, 0.0f, 60.0f - edge, &nx, &ny);
    expect(fabsf(ny - 1.0f) < 1e-3f, "landscape, shorter side: fov spans the height");
    cam.fov_axis = 0;  // vertical: a portrait screen sees less across
    render::crowd_camera(scene, 1080, 1920, vp, right);
    project(vp, 40.0f + edge, 0.0f, 60.0f, &nx, &ny);
    expect(nx > 1.5f, "portrait, vertical: the same point is off the screen");
    cam.fov_axis = 1;  // horizontal
    render::crowd_camera(scene, 1920, 1080, vp, right);
    project(vp, 40.0f + edge, 0.0f, 60.0f, &nx, &ny);
    expect(fabsf(nx - 1.0f) < 1e-3f, "landscape, horizontal: fov spans the width");

    // Higher sees more: the same ground point moves toward the centre.
    cam.fov_axis = 0;
    cam.pitch = radians(60.0f);
    render::crowd_camera(scene, 1080, 1920, vp, right);
    float low_x, low_y;
    project(vp, 45.0f, 0.0f, 60.0f, &low_x, &low_y);
    cam.height = 60.0f;
    render::crowd_camera(scene, 1080, 1920, vp, right);
    project(vp, 45.0f, 0.0f, 60.0f, &nx, &ny);
    expect(nx > 0.0f && nx < low_x * 0.6f, "a higher camera shows more around the target");

    if (g_failures) return 1;
    printf("camera: ok\n");
    return 0;
}
