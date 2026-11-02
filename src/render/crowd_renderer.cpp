#include "render/crowd_renderer.h"

#include "core/math.h"
#include "core/profile.h"

#include <stdio.h>
#include <string.h>

namespace zaro::render {

namespace {

// Mirrors `struct Frame` in shaders/frame.slang (352 bytes, std140).
struct FrameConstants {
    float view_proj[16];
    float planes[6][4];
    float player[4];  // x, z, time, arena size
    float camera[4];  // world position, where distance fog starts
    uint32_t enemy_count;
    uint32_t obstacle_count;
    uint32_t pad[2];
    float sprite[4];  // metres per pixel, fps, frame count, cull radius; zero for cubes
    float right[4];
    float shape[4];        // enemy shapes: kind, sides, size, thickness
    float shape_color[4];  // linear rgb times intensity, opacity
    float shape_glow[4];   // glow, glow strength, additive (1/0), hue jitter
    float ground[4];       // arena colours: ground rgb, 1 when set (0: the grey checkerboard)
    float grid[4];         // grid rgb, spacing
    float obstacle[4];     // obstacle rgb
    float sky[4];          // background and fog rgb
};
static_assert(sizeof(FrameConstants) == 352, "must match shaders/frame.slang");

constexpr uint32_t kCubeVertices = 36;
constexpr uint32_t kQuadVertices = 6;

}  // namespace

struct CrowdRenderer {
    rhi::Device* device = nullptr;
    char shader_dir[512] = {};
    uint32_t max_enemies = 0;
    uint32_t obstacle_count = 0;
    rhi::TextureFormat format = rhi::TextureFormat::kFrame;
    bool shapes = false;  // enemies are CrowdShapes

    rhi::Buffer* frame = nullptr;
    rhi::Buffer* instances = nullptr;
    rhi::Buffer* visible = nullptr;
    rhi::Buffer* args = nullptr;
    rhi::Buffer* obstacles = nullptr;

    // Enemy sprites (CrowdSprites); a 1x1 texture and one frame when the enemies are cubes, so the
    // draw bind group has the same layout either way.
    bool sprites = false;
    float sprite[4] = {};
    rhi::Texture* sprite_atlas = nullptr;
    rhi::Sampler* sprite_sampler = nullptr;
    rhi::Buffer* sprite_frames = nullptr;

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
    d.format = r->format;
    r->ground = rhi::create_render_pipeline(r->device, d);
    d.label = "obstacles";
    d.vertex_entry = "vs_obstacle";
    d.fragment_entry = "fs_lit";
    r->obstacle = rhi::create_render_pipeline(r->device, d);
    rhi::RenderPipelineDesc e = d;
    if (r->shapes) {
        e.label = "enemy shapes";
        e.vertex_entry = "vs_enemy_shape";
        e.fragment_entry = "fs_enemy_shape";
        e.cull_back = false;
        e.alpha_blend = true;
        e.depth_write = false;
    } else if (r->sprites) {
        e.label = "enemy sprites";
        e.vertex_entry = "vs_enemy_sprite";
        e.fragment_entry = "fs_enemy_sprite";
        e.cull_back = false;
    } else {
        e.label = "enemies";
        e.vertex_entry = "vs_enemy";
    }
    r->enemy = rhi::create_render_pipeline(r->device, e);
    d.label = "player";
    d.vertex_entry = "vs_player";
    r->player = rhi::create_render_pipeline(r->device, d);
    rhi::destroy_shader(cull);
    rhi::destroy_shader(draw);
    return true;
}

CrowdRenderer* create_crowd_renderer(rhi::Device* device, const char* shader_dir, uint32_t max_enemies,
                                     const uint32_t* obstacle_cells_xz, uint32_t obstacle_count,
                                     const CrowdLook& look) {
    const CrowdSprites* sprites = look.sprites;
    CrowdRenderer* r = new CrowdRenderer();
    r->device = device;
    r->format = look.format;
    r->shapes = look.shapes;
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

    r->sprites = !r->shapes && sprites && sprites->rgba && sprites->frame_count;
    r->sprite_sampler = rhi::create_sampler(device, true);
    if (r->sprites) {
        r->sprite_atlas = rhi::create_texture(device, sprites->width, sprites->height, rhi::TextureFormat::kRGBA8Srgb,
                                              "enemy sprite atlas");
        rhi::write_texture(device, r->sprite_atlas, 0, 0, sprites->width, sprites->height, sprites->rgba,
                           sprites->width * 4);
        const uint64_t bytes = (uint64_t)sprites->frame_count * sizeof(SpriteFrameInfo);
        r->sprite_frames = rhi::create_buffer(device, bytes, rhi::kBufStorage, "enemy sprite frames");
        rhi::write_buffer(device, r->sprite_frames, 0, sprites->frames, bytes);
        // The cull sphere covers the largest frame from its pivot.
        float reach = 0.0f;
        for (uint32_t i = 0; i < sprites->frame_count; ++i) {
            const SpriteFrameInfo& f = sprites->frames[i];
            const float w = f.size[0] > f.size[1] ? f.size[0] : f.size[1];
            if (w > reach) reach = w;
        }
        r->sprite[0] = sprites->metres_per_pixel;
        r->sprite[1] = sprites->fps;
        r->sprite[2] = (float)sprites->frame_count;
        r->sprite[3] = reach * sprites->metres_per_pixel;
    } else {
        const uint8_t white[4] = {255, 255, 255, 255};
        r->sprite_atlas = rhi::create_texture(device, 1, 1, rhi::TextureFormat::kRGBA8Srgb, "no enemy sprites");
        rhi::write_texture(device, r->sprite_atlas, 0, 0, 1, 1, white, 4);
        const SpriteFrameInfo none = {};
        r->sprite_frames = rhi::create_buffer(device, sizeof(none), rhi::kBufStorage, "no enemy sprite frames");
        rhi::write_buffer(device, r->sprite_frames, 0, &none, sizeof(none));
    }

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
        {4, BindingType::kTexture, rhi::kStageFragment},
        {5, BindingType::kSampler, rhi::kStageFragment},
        {6, BindingType::kStorageRead, rhi::kStageVertex},
    };
    r->cull_layout = rhi::create_bind_layout(device, cull_bindings, 4);
    r->draw_layout = rhi::create_bind_layout(device, draw_bindings, 7);
    const rhi::BindEntry cull_entries[] = {{0, r->frame}, {1, r->instances}, {2, r->visible}, {3, r->args}};
    rhi::BindEntry draw_entries[] = {{0, r->frame}, {1, r->instances}, {2, r->visible}, {3, r->obstacles},
                                     {4, nullptr}, {5, nullptr}, {6, r->sprite_frames}};
    draw_entries[4].texture = r->sprite_atlas;
    draw_entries[5].sampler = r->sprite_sampler;
    r->cull_group = rhi::create_bind_group(device, r->cull_layout, cull_entries, 4);
    r->draw_group = rhi::create_bind_group(device, r->draw_layout, draw_entries, 7);

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
    rhi::destroy_buffer(r->sprite_frames);
    rhi::destroy_texture(r->sprite_atlas);
    rhi::destroy_sampler(r->sprite_sampler);
    delete r;
}

namespace {

Vec3 camera_target(const CrowdScene& scene) {
    if (const CrowdCamera* c = scene.camera) {
        return Vec3{c->target[0] + scene.camera_offset[0], c->target[1], c->target[2] + scene.camera_offset[1]};
    }
    return Vec3{scene.player_x + scene.camera_offset[0], 0.0f, scene.player_z + scene.camera_offset[1]};
}

// How far behind the target the camera is (+z), for its height and pitch; 0 when looking straight down.
float camera_back(const CrowdCamera& c) {
    return c.pitch < 1.5706f ? c.height / tanf(c.pitch) : 0.0f;
}

Vec3 camera_eye(const CrowdScene& scene) {
    if (const CrowdCamera* c = scene.camera) {
        return camera_target(scene) + Vec3{0.0f, c->height, camera_back(*c)} * scene.zoom;
    }
    return camera_target(scene) + Vec3{0.0f, 30.0f, 24.0f} * scene.zoom;
}

// The vertical field of view for this aspect: fov spans the side fov_axis names.
float camera_fov_y(const CrowdCamera& c, float aspect) {
    const bool horizontal = c.fov_axis == 1 || (c.fov_axis == 2 && aspect < 1.0f);
    return horizontal ? 2.0f * atanf(tanf(c.fov * 0.5f) / aspect) : c.fov;
}

Mat4 camera_view_proj(const CrowdScene& scene, uint32_t width, uint32_t height) {
    const float aspect = height ? (float)width / (float)height : 1.0f;
    if (const CrowdCamera* c = scene.camera) {
        // Far enough for the whole arena seen from this height; the screen's up is -z, also from straight above.
        const Vec3 eye = camera_eye(scene);
        const float far_plane = 600.0f + eye.y;
        const Vec3 up = camera_back(*c) > 1e-3f * c->height ? Vec3{0, 1, 0} : Vec3{0, 0, -1};
        return mul(perspective(camera_fov_y(*c, aspect), aspect, 0.5f, far_plane), look_at(eye, camera_target(scene), up));
    }
    return mul(perspective(0.87f, aspect, 0.5f, 600.0f), look_at(camera_eye(scene), camera_target(scene), Vec3{0, 1, 0}));
}

Vec3 camera_right(const CrowdScene& scene) {
    if (scene.camera) return Vec3{1.0f, 0.0f, 0.0f};  // the camera never turns about the vertical
    return normalize(cross(camera_target(scene) - camera_eye(scene), Vec3{0, 1, 0}));
}

}  // namespace

void crowd_camera(const CrowdScene& scene, uint32_t width, uint32_t height, float view_proj[16], float right[3]) {
    const Mat4 vp = camera_view_proj(scene, width, height);
    memcpy(view_proj, vp.m, sizeof(vp.m));
    const Vec3 r = camera_right(scene);
    right[0] = r.x;
    right[1] = r.y;
    right[2] = r.z;
}

void crowd_camera_forward(const CrowdScene& scene, float forward[3]) {
    const Vec3 f = normalize(camera_target(scene) - camera_eye(scene));
    forward[0] = f.x;
    forward[1] = f.y;
    forward[2] = f.z;
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
    fc.camera[3] = scene.camera ? scene.camera->fog : 60.0f;  // where fog starts
    fc.enemy_count = count;
    fc.obstacle_count = r->obstacle_count;
    fc.pad[0] = fc.pad[1] = 0;
    memcpy(fc.sprite, r->sprite, sizeof(fc.sprite));
    const Vec3 right = camera_right(scene);
    fc.right[0] = right.x;
    fc.right[1] = right.y;
    fc.right[2] = right.z;
    fc.right[3] = 0.0f;
    memset(fc.shape, 0, sizeof(float) * 4 * 7);
    if (r->shapes && scene.enemy_shapes) {
        const CrowdShapes& s = *scene.enemy_shapes;
        fc.shape[0] = (float)s.kind;
        fc.shape[1] = (float)s.sides;
        fc.shape[2] = s.size;
        fc.shape[3] = s.thickness;
        memcpy(fc.shape_color, s.color, sizeof(fc.shape_color));
        fc.shape_glow[0] = s.glow;
        fc.shape_glow[1] = s.glow_strength;
        fc.shape_glow[2] = s.additive ? 1.0f : 0.0f;
        fc.shape_glow[3] = s.hue_jitter;
        fc.sprite[3] = s.size * 0.75f + s.glow;  // cull radius (a rect's corner reaches size * 0.71)
    }
    if (const ArenaColors* a = scene.arena) {
        memcpy(fc.ground, a->ground, sizeof(a->ground));
        fc.ground[3] = 1.0f;
        memcpy(fc.grid, a->grid, sizeof(a->grid));
        fc.grid[3] = a->grid_spacing;
        memcpy(fc.obstacle, a->obstacle, sizeof(a->obstacle));
        memcpy(fc.sky, a->sky, sizeof(a->sky));
    }
    const uint32_t args[4] = {r->sprites || r->shapes ? kQuadVertices : kCubeVertices, 0, 0, 0};

    rhi::write_buffer(r->device, r->frame, 0, &fc, sizeof(fc));
    rhi::write_buffer(r->device, r->args, 0, args, sizeof(args));
    if (count) rhi::write_buffer(r->device, r->instances, 0, scene.enemy_instances, count * 16ull);

    rhi::compute_begin(r->device, "cull");
    rhi::compute_dispatch(r->device, r->cull, r->cull_group, (count + 63) / 64);
    rhi::compute_end(r->device);

    float clear[4] = {0.06f, 0.07f, 0.09f, 1.0f};
    if (scene.arena) memcpy(clear, scene.arena->sky, sizeof(scene.arena->sky));
    rhi::render_begin(r->device, "main", clear, scene.keep_depth ? rhi::kDepthStore : rhi::kDepthClear, scene.target);
    rhi::draw(r->device, r->ground, r->draw_group, 6, 1);
    if (r->obstacle_count) rhi::draw(r->device, r->obstacle, r->draw_group, kCubeVertices, r->obstacle_count);
    rhi::draw_indirect(r->device, r->enemy, r->draw_group, r->args);
    if (scene.draw_player) rhi::draw(r->device, r->player, r->draw_group, kCubeVertices, 1);
    rhi::render_end(r->device);
}

}  // namespace zaro::render
