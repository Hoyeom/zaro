#include "app/sprite_layer.h"

#include "core/path.h"
#include "core/png.h"
#include "core/profile.h"
#include "render/sprite_renderer.h"
#include "scene/scene.h"
#include "scene/sprite_sheet.h"
#include "sim/crowd.h"
#include "sim/sprite_components.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

namespace {

constexpr uint32_t kMaxSheets = 64;
constexpr uint32_t kSheetField = 0;      // index of Sprite::sheet in its FieldDesc array
constexpr uint32_t kAnimationField = 1;  // Sprite::animation
constexpr uint32_t kFrameField = 9;      // Sprite::frame

struct SpriteRecord {
    sim::Sprite* sprite;
    const scene::Entity* entity;
    uint32_t sheet;
    char animation[reflect::kStringCapacity];  // the name the current animation was started for
    int animation_index;                       // -1: none, or unknown (already reported)
    uint64_t start_tick;
};

int field_line(const scene::Entity* e, uint32_t field) {
    const scene::ComponentData* c = scene::find_component_data(e, &sim::kSpriteDesc);
    return c && c->field_lines && c->field_lines[field] ? c->field_lines[field] : e->line;
}

}  // namespace

struct SpriteLayer {
    scene::Scene* scene = nullptr;
    scene::SpriteSheet* sheets[kMaxSheets] = {};
    uint32_t sheet_count = 0;
    SpriteRecord* records = nullptr;
    uint32_t record_count = 0;
    uint64_t tick = 0;

    rhi::Device* device = nullptr;
    render::SpriteRenderer* renderer = nullptr;
    uint32_t gpu_sheet[kMaxSheets] = {};
    render::SpriteInstance* instances = nullptr;
    render::SpriteBatch* batches = nullptr;
};

namespace {

// The sheet for a resolved path, loaded once. -1 after a diagnostic.
int sheet_index(SpriteLayer* l, const char* path, DiagList* diags, const char* file, int line) {
    for (uint32_t i = 0; i < l->sheet_count; ++i) {
        if (strcmp(l->sheets[i]->path, path) == 0) return (int)i;
    }
    if (l->sheet_count == kMaxSheets) {
        diag_add(diags, kErrSpriteSheet, file, line, 1, "a scene uses at most %u sprite sheets", kMaxSheets);
        return -1;
    }
    // A missing file is reported where the scene names it; problems inside it at their own lines.
    FILE* f = fopen(path, "rb");
    if (!f) {
        diag_add(diags, kErrSpriteSheet, file, line, 1, "cannot open sprite sheet '%s'", path);
        return -1;
    }
    fclose(f);
    scene::SpriteSheet* sheet = scene::load_sprite_sheet(path, diags);
    if (!sheet) return -1;
    l->sheets[l->sheet_count] = sheet;
    return (int)l->sheet_count++;
}

// Starts the animation the sprite names now. False (with a ZE0902) if its sheet has no such one.
bool start_animation(SpriteLayer* l, SpriteRecord& r, DiagList* diags) {
    snprintf(r.animation, sizeof(r.animation), "%s", r.sprite->animation);
    r.start_tick = l->tick;
    r.animation_index = -1;
    if (!r.animation[0]) return true;
    const scene::SpriteSheet* sheet = l->sheets[r.sheet];
    r.animation_index = scene::find_animation(sheet, r.animation);
    if (r.animation_index >= 0) return true;
    const char* hint = scene::suggest_animation(sheet, r.animation);
    diag_add(diags, kErrSpriteRef, l->scene->path, field_line(r.entity, kAnimationField), 1,
             "entity '%s': sheet '%s' has no animation '%s'%s%s%s", r.entity->name, sheet->path, r.animation,
             hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
    return false;
}

void set_frame(SpriteLayer* l, SpriteRecord& r) {
    if (r.animation_index < 0) return;
    const scene::SheetAnimation& a = l->sheets[r.sheet]->animations[r.animation_index];
    const double seconds = (double)(l->tick - r.start_tick) * (double)sim::kTickSeconds;
    r.sprite->frame = scene::animation_frame(a, seconds, r.sprite->speed);
}

}  // namespace

SpriteLayer* create_sprite_layer(scene::Scene* s, DiagList* diags, bool* failed) {
    *failed = false;
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->entity_count; ++i) count += scene::get_component(&s->entities[i], &sim::kSpriteDesc) ? 1 : 0;
    if (!count) return nullptr;
    SpriteLayer* l = new SpriteLayer();
    l->scene = s;
    l->records = (SpriteRecord*)calloc(count, sizeof(SpriteRecord));
    const uint32_t before = diags->count;
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        const scene::Entity* e = &s->entities[i];
        const scene::ComponentData* c = scene::find_component_data(e, &sim::kSpriteDesc);
        if (!c) continue;
        sim::Sprite* sprite = (sim::Sprite*)c->data;
        if (!sprite->sheet[0]) {
            diag_add(diags, kErrSpriteSheet, s->path, c->line ? c->line : e->line, 1,
                     "entity '%s': Sprite needs a sheet", e->name);
            continue;
        }
        char path[260];
        path_join_relative(s->path, sprite->sheet, path, sizeof(path));
        const int sheet = sheet_index(l, path, diags, s->path, field_line(e, kSheetField));
        if (sheet < 0) continue;
        if (sprite->frame >= l->sheets[sheet]->frame_count) {
            diag_add(diags, kErrSpriteRef, s->path, field_line(e, kFrameField), 1,
                     "entity '%s': frame %u is past the last frame (%u) of '%s'", e->name, sprite->frame,
                     l->sheets[sheet]->frame_count - 1, path);
            continue;
        }
        SpriteRecord& r = l->records[l->record_count++];
        r.sprite = sprite;
        r.entity = e;
        r.sheet = (uint32_t)sheet;
        if (start_animation(l, r, diags)) set_frame(l, r);
    }
    if (diags->count != before) {
        *failed = true;
        destroy_sprite_layer(l);
        return nullptr;
    }
    return l;
}

void destroy_sprite_layer(SpriteLayer* l) {
    if (!l) return;
    render::destroy_sprite_renderer(l->renderer);
    for (uint32_t i = 0; i < l->sheet_count; ++i) scene::free_sprite_sheet(l->sheets[i]);
    free(l->records);
    free(l->instances);
    free(l->batches);
    delete l;
}

bool step_sprites(SpriteLayer* l, uint64_t tick, DiagList* diags) {
    if (!l) return true;
    l->tick = tick;
    bool ok = true;
    for (uint32_t i = 0; i < l->record_count; ++i) {
        SpriteRecord& r = l->records[i];
        if (strcmp(r.animation, r.sprite->animation) != 0) ok = start_animation(l, r, diags) && ok;
        set_frame(l, r);
        const uint32_t frames = l->sheets[r.sheet]->frame_count;
        if (r.sprite->frame >= frames) r.sprite->frame = frames - 1;  // a script set it past the end
    }
    return ok;
}

bool attach_sprite_gpu(SpriteLayer* l, rhi::Device* device, const char* shader_dir) {
    l->device = device;
    l->renderer = render::create_sprite_renderer(device, shader_dir);
    if (!l->renderer) return false;
    for (uint32_t i = 0; i < l->sheet_count; ++i) {
        const scene::SpriteSheet* sheet = l->sheets[i];
        uint8_t* rgba = nullptr;
        uint32_t w = 0, h = 0;
        if (!read_png_rgba8(sheet->image, &rgba, &w, &h)) {
            log_error(kErrSpriteImage, "%s: cannot read image '%s'", sheet->path, sheet->image);
            return false;
        }
        render::SpriteFrameInfo* frames =
            (render::SpriteFrameInfo*)malloc(sizeof(render::SpriteFrameInfo) * sheet->frame_count);
        for (uint32_t f = 0; f < sheet->frame_count; ++f) {
            const scene::SheetFrame& sf = sheet->frames[f];
            render::SpriteFrameInfo& fi = frames[f];
            fi.uv[0] = (float)sf.x / (float)w;
            fi.uv[1] = (float)sf.y / (float)h;
            fi.uv[2] = (float)(sf.x + sf.w) / (float)w;
            fi.uv[3] = (float)(sf.y + sf.h) / (float)h;
            fi.size[0] = (float)sf.w;
            fi.size[1] = (float)sf.h;
            fi.pivot[0] = sf.pivot[0];
            fi.pivot[1] = sf.pivot[1];
        }
        l->gpu_sheet[i] = render::add_sprite_sheet(l->renderer, rgba, w, h, frames, sheet->frame_count, sheet->path);
        free(frames);
        free_png(rgba);
    }
    l->instances = (render::SpriteInstance*)malloc(sizeof(render::SpriteInstance) * l->record_count);
    l->batches = (render::SpriteBatch*)malloc(sizeof(render::SpriteBatch) * l->record_count);
    return true;
}

bool reload_sprite_layer_shaders(SpriteLayer* l) { return l && l->renderer && render::reload_sprite_shaders(l->renderer); }

bool has_world_sprites(const SpriteLayer* l) {
    for (uint32_t i = 0; l && i < l->record_count; ++i) {
        if (l->records[i].sprite->space == sim::kSpriteWorld) return true;
    }
    return false;
}

void render_sprite_layer(SpriteLayer* l, const float view_proj[16], const float right[3]) {
    if (!l || !l->renderer) return;
    ZARO_ZONE("app.sprites");
    uint32_t n = 0, batches = 0;
    // World sprites grouped by sheet (depth sorts them), then screen sprites in entity order.
    for (int pass = 0; pass < 2; ++pass) {
        const uint32_t space = pass == 0 ? sim::kSpriteWorld : sim::kSpriteScreen;
        const uint32_t sheet_passes = pass == 0 ? l->sheet_count : 1;
        for (uint32_t sp = 0; sp < sheet_passes; ++sp) {
            for (uint32_t i = 0; i < l->record_count; ++i) {
                const SpriteRecord& r = l->records[i];
                const sim::Sprite& s = *r.sprite;
                if (s.space != space || !s.visible || (pass == 0 && r.sheet != sp)) continue;
                const scene::SpriteSheet* sheet = l->sheets[r.sheet];
                render::SpriteInstance& inst = l->instances[n];
                memcpy(inst.position, s.position, sizeof(inst.position));
                inst.scale = pass == 0 ? s.scale / sheet->pixels_per_unit : s.scale;
                inst.frame = s.frame;
                inst.color = s.color;
                inst.flags = s.flip_x ? render::kSpriteFlipX : 0u;
                inst.pad = 0.0f;
                const uint32_t gpu = l->gpu_sheet[r.sheet];
                render::SpriteBatch* last = batches ? &l->batches[batches - 1] : nullptr;
                if (last && last->sheet == gpu && last->screen == (pass == 1) && last->first + last->count == n) {
                    last->count++;
                } else {
                    l->batches[batches++] = {gpu, n, 1, pass == 1};
                }
                ++n;
            }
        }
    }
    render::SpriteDraw draw;
    draw.instances = l->instances;
    draw.instance_count = n;
    draw.batches = l->batches;
    draw.batch_count = batches;
    memcpy(draw.view_proj, view_proj, sizeof(draw.view_proj));
    memcpy(draw.right, right, sizeof(draw.right));
    render::render_sprites(l->renderer, draw);
}

}  // namespace zaro::app
