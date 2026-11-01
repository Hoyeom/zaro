#include "app/shape_layer.h"

#include "core/log.h"
#include "core/profile.h"
#include "render/shape_renderer.h"
#include "scene/scene.h"
#include "sim/crowd.h"
#include "sim/shape_components.h"
#include "sim/sprite_components.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

namespace {

constexpr uint32_t kMaxTrail = 120;  // samples, one per tick: Shape::trail is at most 2 s
constexpr float kDegrees = 3.14159265f / 180.0f;
constexpr uint32_t kFollowField = 3;  // index of Shape::follow in its FieldDesc array

}  // namespace

// Outside the anonymous namespace, as ShapeLayer holds them.
struct ShapeRecord {
    sim::Shape* shape;
    float trail[kMaxTrail][2];  // x, z per tick, newest at trail_head - 1
    uint32_t trail_head;
    uint32_t trail_count;
};

struct ShapeLayer {
    scene::Scene* scene = nullptr;
    ShapeRecord* records = nullptr;
    uint32_t count = 0;
    const sim::CrowdShape* crowd = nullptr;
    const sim::ArenaLook* arena = nullptr;
    render::CrowdShapes crowd_shapes;
    render::ArenaColors arena_colors;

    rhi::Device* device = nullptr;
    render::ShapeRenderer* renderer = nullptr;
    render::ShapeInstance* instances = nullptr;
    uint32_t capacity = 0;
};

namespace {

void linear_rgb(uint32_t color, float intensity, float out[3]) {
    float c[4];
    render::shape_color(color, intensity, c);
    memcpy(out, c, sizeof(float) * 3);
}

render::ShapeInstance make_instance(const sim::Shape& s, float x, float y, float z, float seconds) {
    render::ShapeInstance in = {};
    in.position[0] = x;
    in.position[1] = y;
    in.position[2] = z;
    in.rotation = (s.rotation + s.spin * seconds) * kDegrees;
    in.size[0] = s.size[0];
    in.size[1] = s.size[1];
    in.thickness = s.thickness;
    in.glow = s.glow;
    render::shape_color(s.color, s.intensity, in.color);
    if (s.flash > 0.0f) {
        // A hit flash turns the shape white, bright enough to glow under Bloom.
        const float white = s.intensity > 2.0f ? s.intensity : 2.0f;
        for (int i = 0; i < 3; ++i) in.color[i] += (white - in.color[i]) * s.flash;
    }
    in.kind = s.kind;
    in.sides = s.sides;
    in.flags = (s.blend == sim::kShapeBlendAdd ? render::kShapeAdditive : 0u) |
               (s.space == sim::kSpriteScreen ? render::kShapeScreen : 0u);
    in.glow_strength = s.glow_strength;
    return in;
}

// The trail as line segments from the shape's position now back through the tick samples,
// thinning and fading toward the end. Returns how many instances it wrote.
uint32_t add_trail(const ShapeRecord& r, float x, float y, float z, render::ShapeInstance* out) {
    const sim::Shape& s = *r.shape;
    const uint32_t wanted = (uint32_t)(s.trail / sim::kTickSeconds + 0.5f);
    const uint32_t n = wanted < r.trail_count ? wanted : r.trail_count;
    if (!n) return 0;
    const float size = s.kind == sim::kShapeKindRect && s.size[1] < s.size[0] ? s.size[1] : s.size[0];
    const float width = s.trail_width > 0.0f ? s.trail_width : size / 3.0f;
    float px = x, pz = z;
    uint32_t written = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const float* p = r.trail[(r.trail_head + kMaxTrail - 1 - i) % kMaxTrail];
        const float dx = p[0] - px, dz = p[1] - pz;
        const float len = sqrtf(dx * dx + dz * dz);
        if (len > 1e-4f) {
            const float fade = 1.0f - ((float)i + 0.5f) / (float)n;
            render::ShapeInstance& in = out[written++];
            in = {};
            in.position[0] = (px + p[0]) * 0.5f;
            in.position[1] = y;
            in.position[2] = (pz + p[1]) * 0.5f;
            in.rotation = atan2f(dz, dx);  // +x turns toward +z
            in.size[0] = len;
            in.size[1] = 0.0f;
            in.thickness = width * fade;
            in.glow = width * 0.5f * fade;
            render::shape_color(s.color, s.intensity, in.color);
            in.color[3] *= fade;
            in.kind = render::kShapeLine;
            in.flags = render::kShapeAdditive;
            in.glow_strength = s.glow_strength;
        }
        px = p[0];
        pz = p[1];
    }
    return written;
}

}  // namespace

ShapeLayer* create_shape_layer(scene::Scene* s, DiagList* diags, bool* failed) {
    *failed = false;
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->entity_count; ++i) count += scene::get_component(&s->entities[i], &sim::kShapeDesc) ? 1 : 0;
    const scene::Entity* crowd_entity = nullptr;
    const auto* crowd = (const sim::CrowdShape*)scene::first_component(s, &sim::kCrowdShapeDesc, &crowd_entity);
    const auto* arena = (const sim::ArenaLook*)scene::first_component(s, &sim::kArenaLookDesc, nullptr);
    if (!count && !crowd && !arena) return nullptr;

    const uint32_t before = diags->count;
    const scene::Entity* sprite_entity = nullptr;
    if (crowd && scene::first_component(s, &sim::kCrowdSpriteDesc, &sprite_entity)) {
        diag_add(diags, kErrShape, s->path, crowd_entity->line, 1,
                 "entity '%s': the crowd has both CrowdShape and CrowdSprite (on '%s'); keep one", crowd_entity->name,
                 sprite_entity->name);
    }
    ShapeLayer* l = new ShapeLayer();
    l->scene = s;
    l->crowd = crowd;
    l->arena = arena;
    l->records = (ShapeRecord*)calloc(count ? count : 1, sizeof(ShapeRecord));
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        scene::Entity& e = s->entities[i];
        scene::ComponentData* c = (scene::ComponentData*)scene::find_component_data(&e, &sim::kShapeDesc);
        if (!c) continue;
        sim::Shape* shape = (sim::Shape*)c->data;
        if (shape->follow == sim::kShapeFollowPlayer && shape->space == sim::kSpriteScreen) {
            const int line = c->field_lines && c->field_lines[kFollowField] ? c->field_lines[kFollowField] : e.line;
            diag_add(diags, kErrShape, s->path, line, 1,
                     "entity '%s': a screen shape cannot follow the player; use space = \"world\"", e.name);
        }
        l->records[l->count++].shape = shape;
    }
    if (diags->count != before) {
        *failed = true;
        destroy_shape_layer(l);
        return nullptr;
    }
    return l;
}

void destroy_shape_layer(ShapeLayer* l) {
    if (!l) return;
    render::destroy_shape_renderer(l->renderer);
    free(l->instances);
    free(l->records);
    delete l;
}

void step_shapes(ShapeLayer* l, uint64_t, float player_x, float player_z) {
    if (!l) return;
    for (uint32_t i = 0; i < l->count; ++i) {
        ShapeRecord& r = l->records[i];
        sim::Shape& s = *r.shape;
        if (s.flash > 0.0f) {
            s.flash -= s.flash_decay * sim::kTickSeconds;
            if (s.flash < 0.0f) s.flash = 0.0f;
        }
        if (s.trail <= 0.0f || s.space == sim::kSpriteScreen) {
            r.trail_count = 0;
            continue;
        }
        const bool follow = s.follow == sim::kShapeFollowPlayer;
        r.trail[r.trail_head][0] = s.position[0] + (follow ? player_x : 0.0f);
        r.trail[r.trail_head][1] = s.position[2] + (follow ? player_z : 0.0f);
        r.trail_head = (r.trail_head + 1) % kMaxTrail;
        if (r.trail_count < kMaxTrail) r.trail_count++;
    }
}

void flash_player_shapes(ShapeLayer* l, float amount) {
    if (!l) return;
    for (uint32_t i = 0; i < l->count; ++i) {
        sim::Shape& s = *l->records[i].shape;
        if (s.follow == sim::kShapeFollowPlayer && s.flash < amount) s.flash = amount > 1.0f ? 1.0f : amount;
    }
}

bool crowd_has_shapes(const ShapeLayer* l) { return l && l->crowd; }

void shape_crowd_scene(ShapeLayer* l, render::CrowdScene* scene) {
    if (!l) return;
    if (const sim::CrowdShape* c = l->crowd) {
        render::CrowdShapes& out = l->crowd_shapes;
        out.kind = c->kind;
        out.sides = c->sides;
        out.size = c->size;
        out.thickness = c->thickness;
        render::shape_color(c->color, c->intensity, out.color);
        out.glow = c->glow;
        out.glow_strength = c->glow_strength;
        out.hue_jitter = c->hue_jitter;
        out.additive = c->blend == sim::kShapeBlendAdd;
        scene->enemy_shapes = &out;
    }
    if (const sim::ArenaLook* a = l->arena) {
        render::ArenaColors& out = l->arena_colors;
        linear_rgb(a->ground, 1.0f, out.ground);
        linear_rgb(a->grid, a->grid_intensity, out.grid);
        out.grid_spacing = a->grid_spacing;
        linear_rgb(a->obstacle, a->obstacle_intensity, out.obstacle);
        linear_rgb(a->sky, 1.0f, out.sky);
        scene->arena = &out;
    }
    for (uint32_t i = 0; i < l->count; ++i) {
        const sim::Shape& s = *l->records[i].shape;
        if (s.visible && s.follow == sim::kShapeFollowPlayer) scene->draw_player = false;
        if (s.space == sim::kSpriteWorld) scene->keep_depth = true;
    }
}

bool attach_shape_gpu(ShapeLayer* l, rhi::Device* device, const char* shader_dir, rhi::TextureFormat format) {
    if (!l) return true;
    l->device = device;
    if (!l->count) return true;  // only the crowd's look or the arena's colours
    l->renderer = render::create_shape_renderer(device, shader_dir, format);
    return l->renderer != nullptr;
}

bool reload_shape_layer_shaders(ShapeLayer* l) { return !l || !l->renderer || render::reload_shape_shaders(l->renderer); }

void render_shape_layer(ShapeLayer* l, const float view_proj[16], rhi::Texture* target, float player_x,
                        float player_z, float seconds, bool keep_depth) {
    if (!l || !l->renderer) return;
    ZARO_ZONE("app.shapes");
    uint32_t need = 0;
    for (uint32_t i = 0; i < l->count; ++i) need += 1 + l->records[i].trail_count;
    if (need > l->capacity) {
        free(l->instances);
        l->capacity = need;
        l->instances = (render::ShapeInstance*)malloc(sizeof(render::ShapeInstance) * need);
    }
    uint32_t n = 0, world = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const uint32_t space = pass == 0 ? sim::kSpriteWorld : sim::kSpriteScreen;
        for (uint32_t i = 0; i < l->count; ++i) {
            const ShapeRecord& r = l->records[i];
            const sim::Shape& s = *r.shape;
            if (!s.visible || s.space != space) continue;
            const bool follow = space == sim::kSpriteWorld && s.follow == sim::kShapeFollowPlayer;
            const float x = s.position[0] + (follow ? player_x : 0.0f);
            const float z = s.position[2] + (follow ? player_z : 0.0f);
            if (space == sim::kSpriteWorld && s.trail > 0.0f) n += add_trail(r, x, s.position[1], z, l->instances + n);
            l->instances[n++] = make_instance(s, x, s.position[1], z, seconds);
        }
        if (pass == 0) world = n;
    }
    render::ShapeDraw draw;
    draw.instances = l->instances;
    draw.world_count = world;
    draw.screen_count = n - world;
    memcpy(draw.view_proj, view_proj, sizeof(draw.view_proj));
    draw.target = target;
    draw.keep_depth = keep_depth;
    render::render_shapes(l->renderer, draw);
}

}  // namespace zaro::app
