#include "render/crowd_renderer.h"

#include "core/math.h"
#include "core/profile.h"

#include <stdio.h>
#include <string.h>

namespace zaro::render {

namespace {

// Mirrors `struct Frame` in shaders/frame.slang (208 bytes, std140).
struct FrameConstants {
    float view_proj[16];
    float planes[6][4];
    float player[4];  // x, z, time, arena size
    float camera[4];
    uint32_t enemy_count;
    uint32_t obstacle_count;
    uint32_t pad[2];
};
static_assert(sizeof(FrameConstants) == 208, "must match shaders/frame.slang");

constexpr uint32_t kCubeVertices = 36;

}  // namespace

struct CrowdRenderer {
    rhi::Device* device = nullptr;
    char shader_dir[512] = {};
    uint32_t max_enemies = 0;
    uint32_t obstacle_count = 0;

    rhi::Buffer* frame = nullptr;
    rhi::Buffer* instances = nullptr;
    rhi::Buffer* visible = nullptr;
    rhi::Buffer* args = nullptr;
    rhi::Buffer* obstacles = nullptr;

    rhi::BindLayout* cull_layout = nullptr;
    rhi::BindLayout* draw_layout = nullptr;
    rhi::BindGroup* cull_group = nullptr;
    rhi::BindGroup* draw_group = nullptr;

    rhi::ComputePipeline* cull = nullptr;
    rhi::RenderPipeline* ground = nullptr;
    rhi::RenderPipeline* obstacle = nullptr;
    rhi::RenderPipeline* enemy = nullptr;
    rhi::RenderPipeline* player = nullptr;
};

namespace {

void destroy_pipelines(CrowdRenderer* r) {
    rhi::destroy_compute_pipeline(r->cull);
    rhi::destroy_render_pipeline(r->ground);
    rhi::destroy_render_pipeline(r->obstacle);
    rhi::destroy_render_pipeline(r->enemy);
    rhi::destroy_render_pipeline(r->player);
    r->cull = nullptr;
    r->ground = r->obstacle = r->enemy = r->player = nullptr;
}

}  // namespace

bool reload_shaders(CrowdRenderer* r) {
    char path[600];
    snprintf(path, sizeof(path), "%scrowd_cull.wgsl", r->shader_dir);
    rhi::Shader* cull = rhi::load_shader(r->device, path);
    snprintf(path, sizeof(path), "%scrowd_draw.wgsl", r->shader_dir);
    rhi::Shader* draw = rhi::load_shader(r->device, path);
    if (!cull || !draw) {
        rhi::destroy_shader(cull);
        rhi::destroy_shader(draw);
        return false;
    }
    destroy_pipelines(r);
    r->cull = rhi::create_compute_pipeline(r->device, {"crowd cull", cull, "cull", r->cull_layout});
    rhi::RenderPipelineDesc d{"ground", draw, "vs_ground", "fs_ground", r->draw_layout};
    r->ground = rhi::create_render_pipeline(r->device, d);
    d = {"obstacles", draw, "vs_obstacle", "fs_lit", r->draw_layout};
    r->obstacle = rhi::create_render_pipeline(r->device, d);
    d = {"enemies", draw, "vs_enemy", "fs_lit", r->draw_layout};
    r->enemy = rhi::create_render_pipeline(r->device, d);
    d = {"player", draw, "vs_player", "fs_lit", r->draw_layout};
    r->player = rhi::create_render_pipeline(r->device, d);
    rhi::destroy_shader(cull);
    rhi::destroy_shader(draw);
    return true;
}

CrowdRenderer* create_crowd_renderer(rhi::Device* device, const char* shader_dir, uint32_t max_enemies,
                                     const uint32_t* obstacle_cells_xz, uint32_t obstacle_count) {
    CrowdRenderer* r = new CrowdRenderer();
    r->device = device;
    snprintf(r->shader_dir, sizeof(r->shader_dir), "%s", shader_dir);
    r->max_enemies = max_enemies;
    r->obstacle_count = obstacle_count;

    const uint64_t n = max_enemies ? max_enemies : 1;
    r->frame = rhi::create_buffer(device, sizeof(FrameConstants), rhi::kBufUniform, "frame constants");
    r->instances = rhi::create_buffer(device, n * 16, rhi::kBufStorage, "enemy instances");
    r->visible = rhi::create_buffer(device, n * 4, rhi::kBufStorage, "visible enemies");
    r->args = rhi::create_buffer(device, 16, rhi::kBufStorage | rhi::kBufIndirect, "enemy draw args");
    r->obstacles = rhi::create_buffer(device, (obstacle_count ? obstacle_count : 1) * 8ull, rhi::kBufStorage,
                                      "obstacle cells");
    if (obstacle_count) rhi::write_buffer(device, r->obstacles, 0, obstacle_cells_xz, obstacle_count * 8ull);

    using rhi::BindingType;
    const rhi::BindingDesc cull_bindings[] = {
        {0, BindingType::kUniform, rhi::kStageCompute},
        {1, BindingType::kStorageRead, rhi::kStageCompute},
        {2, BindingType::kStorageReadWrite, rhi::kStageCompute},
        {3, BindingType::kStorageReadWrite, rhi::kStageCompute},
    };
    const rhi::BindingDesc draw_bindings[] = {
        {0, BindingType::kUniform, rhi::kStageVertex | rhi::kStageFragment},
        {1, BindingType::kStorageRead, rhi::kStageVertex},
        {2, BindingType::kStorageRead, rhi::kStageVertex},
        {3, BindingType::kStorageRead, rhi::kStageVertex},
    };
    r->cull_layout = rhi::create_bind_layout(device, cull_bindings, 4);
    r->draw_layout = rhi::create_bind_layout(device, draw_bindings, 4);
    const rhi::BindEntry cull_entries[] = {{0, r->frame}, {1, r->instances}, {2, r->visible}, {3, r->args}};
    const rhi::BindEntry draw_entries[] = {{0, r->frame}, {1, r->instances}, {2, r->visible}, {3, r->obstacles}};
    r->cull_group = rhi::create_bind_group(device, r->cull_layout, cull_entries, 4);
    r->draw_group = rhi::create_bind_group(device, r->draw_layout, draw_entries, 4);

    if (!reload_shaders(r)) {
        destroy_crowd_renderer(r);
        return nullptr;
    }
    return r;
}

void destroy_crowd_renderer(CrowdRenderer* r) {
    if (!r) return;
    destroy_pipelines(r);
    rhi::destroy_bind_group(r->cull_group);
    rhi::destroy_bind_group(r->draw_group);
    rhi::destroy_bind_layout(r->cull_layout);
    rhi::destroy_bind_layout(r->draw_layout);
    rhi::destroy_buffer(r->frame);
    rhi::destroy_buffer(r->instances);
    rhi::destroy_buffer(r->visible);
    rhi::destroy_buffer(r->args);
    rhi::destroy_buffer(r->obstacles);
    delete r;
}

namespace {

Vec3 camera_eye(const CrowdScene& scene) {
    return Vec3{scene.player_x, 0.0f, scene.player_z} + Vec3{0.0f, 30.0f, 24.0f} * scene.zoom;
}

Mat4 camera_view_proj(const CrowdScene& scene, uint32_t width, uint32_t height) {
    const Vec3 target{scene.player_x, 0.0f, scene.player_z};
    const float aspect = height ? (float)width / (float)height : 1.0f;
    return mul(perspective(0.87f, aspect, 0.5f, 600.0f), look_at(camera_eye(scene), target, Vec3{0, 1, 0}));
}

}  // namespace

void crowd_camera(const CrowdScene& scene, uint32_t width, uint32_t height, float view_proj[16], float right[3]) {
    const Mat4 vp = camera_view_proj(scene, width, height);
    memcpy(view_proj, vp.m, sizeof(vp.m));
    const Vec3 target{scene.player_x, 0.0f, scene.player_z};
    const Vec3 r = normalize(cross(target - camera_eye(scene), Vec3{0, 1, 0}));
    right[0] = r.x;
    right[1] = r.y;
    right[2] = r.z;
}

void render_crowd(CrowdRenderer* r, const CrowdScene& scene) {
    ZARO_ZONE("render.crowd");
    uint32_t width, height;
    rhi::target_size(r->device, &width, &height);
    const uint32_t count = scene.enemy_count < r->max_enemies ? scene.enemy_count : r->max_enemies;

    const Vec3 eye = camera_eye(scene);
    const Mat4 vp = camera_view_proj(scene, width, height);

    FrameConstants fc;
    memcpy(fc.view_proj, vp.m, sizeof(fc.view_proj));
    frustum_planes(vp, fc.planes);
    fc.player[0] = scene.player_x;
    fc.player[1] = scene.player_z;
    fc.player[2] = scene.time_seconds;
    fc.player[3] = scene.arena_size;
    fc.camera[0] = eye.x;
    fc.camera[1] = eye.y;
    fc.camera[2] = eye.z;
    fc.camera[3] = 1.0f;
    fc.enemy_count = count;
    fc.obstacle_count = r->obstacle_count;
    fc.pad[0] = fc.pad[1] = 0;
    const uint32_t args[4] = {kCubeVertices, 0, 0, 0};

    rhi::write_buffer(r->device, r->frame, 0, &fc, sizeof(fc));
    rhi::write_buffer(r->device, r->args, 0, args, sizeof(args));
    if (count) rhi::write_buffer(r->device, r->instances, 0, scene.enemy_instances, count * 16ull);

    rhi::compute_begin(r->device, "cull");
    rhi::compute_dispatch(r->device, r->cull, r->cull_group, (count + 63) / 64);
    rhi::compute_end(r->device);

    const float clear[4] = {0.06f, 0.07f, 0.09f, 1.0f};
    rhi::render_begin(r->device, "main", clear, scene.keep_depth ? rhi::kDepthStore : rhi::kDepthClear);
    rhi::draw(r->device, r->ground, r->draw_group, 6, 1);
    if (r->obstacle_count) rhi::draw(r->device, r->obstacle, r->draw_group, kCubeVertices, r->obstacle_count);
    rhi::draw_indirect(r->device, r->enemy, r->draw_group, r->args);
    rhi::draw(r->device, r->player, r->draw_group, kCubeVertices, 1);
    rhi::render_end(r->device);
}

}  // namespace zaro::render
