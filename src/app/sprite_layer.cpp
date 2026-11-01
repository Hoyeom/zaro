#include "app/sprite_layer.h"

#include "core/path.h"
#include "platform/platform.h"
#include "core/png.h"
#include "core/profile.h"
#include "render/sprite_renderer.h"
#include "scene/scene.h"
#include "scene/sprite_sheet.h"
#include "scene/world.h"
#include "sim/crowd.h"
#include "sim/sprite_components.h"
#if ZARO_HAS_COOK
#include "cook/svg.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

namespace {

constexpr uint32_t kMaxSheets = 64;
constexpr uint32_t kSheetField = 0;      // index of Sprite::sheet in its FieldDesc array
constexpr uint32_t kAnimationField = 1;  // Sprite::animation
constexpr uint32_t kFrameField = 9;      // Sprite::frame
constexpr uint32_t kCrowdSheetField = 0;      // CrowdSprite::sheet
constexpr uint32_t kCrowdAnimationField = 1;  // CrowdSprite::animation

int field_line(const scene::Entity* e, uint32_t field, const reflect::ComponentDesc* desc = &sim::kSpriteDesc) {
    const scene::ComponentData* c = scene::find_component_data(e, desc);
    return c && c->field_lines && c->field_lines[field] ? c->field_lines[field] : e->line;
}

}  // namespace

// Outside the anonymous namespace, as SpriteLayer holds them.
struct SpriteRecord {
    sim::Sprite* sprite;
    uint32_t entity_id;  // records are in id order, like the scene's entities
    uint32_t sheet;
    char animation[reflect::kStringCapacity];  // the name the current animation was started for
    int animation_index;                       // -1: none, or unknown (already reported)
    uint64_t start_tick;
};

struct SpriteLayer {
    scene::Scene* scene = nullptr;
    scene::SpriteSheet* sheets[kMaxSheets] = {};
    uint32_t sheet_count = 0;
    SpriteRecord* records = nullptr;
    uint32_t record_count = 0;
    uint64_t tick = 0;
    uint64_t generation = 0;     // the scene's generation the records were built for
    bool spawnable = false;      // a prefab scripts can spawn has a Sprite (D-031)
    uint32_t instance_capacity = 0;
    uint32_t gpu_sheet_count = 0;  // sheets uploaded so far
    // The crowd's CrowdSprite: its sheet, animation (-1: every frame in order) and scale.
    int crowd_sheet = -1;
    int crowd_animation = -1;
    float crowd_scale = 1.0f;

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
    const scene::Entity* e = scene::entity_by_id(l->scene, r.entity_id);
    diag_add(diags, kErrSpriteRef, l->scene->path, e ? field_line(e, kAnimationField) : 0, 1,
             "entity '%s': sheet '%s' has no animation '%s'%s%s%s", e ? e->name : "?", sheet->path, r.animation,
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

namespace {

const sim::Sprite* prefab_sprite(const scene::SpawnPrefab& p) {
    for (uint32_t i = 0; i < p.component_count; ++i) {
        if (p.components[i].desc == &sim::kSpriteDesc) return (const sim::Sprite*)p.components[i].data;
    }
    return nullptr;
}

// Appends the record of an entity's Sprite, if it has a valid one. `keep` is its record from
// before a re-sync, whose sheet and animation carry over. False after a diagnostic.
bool add_record(SpriteLayer* l, const scene::Entity* e, const SpriteRecord* keep, DiagList* diags) {
    const scene::ComponentData* c = scene::find_component_data(e, &sim::kSpriteDesc);
    if (!c || e->destroyed) return true;
    if (keep) {
        l->records[l->record_count++] = *keep;
        return true;
    }
    const scene::Scene* s = l->scene;
    sim::Sprite* sprite = (sim::Sprite*)c->data;
    if (!sprite->sheet[0]) {
        diag_add(diags, kErrSpriteSheet, s->path, c->line ? c->line : e->line, 1, "entity '%s': Sprite needs a sheet",
                 e->name);
        return false;
    }
    char path[260];
    path_join_relative(s->path, sprite->sheet, path, sizeof(path));
    const int sheet = sheet_index(l, path, diags, s->path, field_line(e, kSheetField));
    if (sheet < 0) return false;
    if (sprite->frame >= l->sheets[sheet]->frame_count) {
        diag_add(diags, kErrSpriteRef, s->path, field_line(e, kFrameField), 1,
                 "entity '%s': frame %u is past the last frame (%u) of '%s'", e->name, sprite->frame,
                 l->sheets[sheet]->frame_count - 1, path);
        return false;
    }
    SpriteRecord& r = l->records[l->record_count++];
    r = SpriteRecord{};
    r.sprite = sprite;
    r.entity_id = e->id;
    r.sheet = (uint32_t)sheet;
    if (!start_animation(l, r, diags)) return false;
    set_frame(l, r);
    return true;
}

bool upload_sheet(SpriteLayer* l, uint32_t i);
bool ensure_gpu(SpriteLayer* l);

// Rebuilds the records after entities were spawned or removed (Scene::generation changed),
// keeping the animation state of sprites that are still there.
bool sync_records(SpriteLayer* l, DiagList* diags) {
    if (l->generation == l->scene->generation) return true;
    l->generation = l->scene->generation;
    const scene::Scene* s = l->scene;
    SpriteRecord* old = l->records;
    const uint32_t old_count = l->record_count;
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->entity_count; ++i) count += scene::get_component(&s->entities[i], &sim::kSpriteDesc) ? 1 : 0;
    l->records = (SpriteRecord*)calloc(count ? count : 1, sizeof(SpriteRecord));
    l->record_count = 0;
    bool ok = true;
    uint32_t j = 0;
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        const scene::Entity* e = &s->entities[i];
        while (j < old_count && old[j].entity_id < e->id) ++j;
        ok = add_record(l, e, j < old_count && old[j].entity_id == e->id ? &old[j] : nullptr, diags) && ok;
    }
    free(old);
    return (!l->device || ensure_gpu(l)) && ok;
}

}  // namespace

SpriteLayer* create_sprite_layer(scene::Scene* s, DiagList* diags, bool* failed) {
    *failed = false;
    uint32_t count = 0;
    const scene::Entity* crowd = nullptr;
    const auto* look = (const sim::CrowdSprite*)scene::first_component(s, &sim::kCrowdSpriteDesc, &crowd);
    for (uint32_t i = 0; i < s->entity_count; ++i) count += scene::get_component(&s->entities[i], &sim::kSpriteDesc) ? 1 : 0;
    bool spawnable = false;
    for (uint32_t i = 0; i < s->prefab_count; ++i) spawnable = spawnable || prefab_sprite(s->prefabs[i]);
    if (!count && !look && !spawnable) return nullptr;
    SpriteLayer* l = new SpriteLayer();
    l->scene = s;
    l->spawnable = spawnable;
    l->records = (SpriteRecord*)calloc(count ? count : 1, sizeof(SpriteRecord));
    const uint32_t before = diags->count;
    // Sheets of sprites scripts can spawn are loaded up front, so their art is ready on the GPU.
    for (uint32_t i = 0; i < s->prefab_count; ++i) {
        const sim::Sprite* sprite = prefab_sprite(s->prefabs[i]);
        if (!sprite) continue;
        if (!sprite->sheet[0]) {
            diag_add(diags, kErrSpriteSheet, s->prefabs[i].path, 1, 1, "prefab '%s': Sprite needs a sheet",
                     s->prefabs[i].name);
            continue;
        }
        char path[260];
        path_join_relative(s->path, sprite->sheet, path, sizeof(path));
        sheet_index(l, path, diags, s->prefabs[i].path, 1);
    }
    if (look) {
        char path[260];
        path_join_relative(s->path, look->sheet, path, sizeof(path));
        const int sheet = look->sheet[0] ? sheet_index(l, path, diags, s->path, field_line(crowd, kCrowdSheetField,
                                                                                         &sim::kCrowdSpriteDesc))
                                         : -1;
        if (!look->sheet[0]) {
            diag_add(diags, kErrSpriteSheet, s->path, crowd->line, 1, "entity '%s': CrowdSprite needs a sheet",
                     crowd->name);
        } else if (sheet >= 0) {
            l->crowd_sheet = sheet;
            l->crowd_scale = look->scale;
            const scene::SpriteSheet* sh = l->sheets[sheet];
            if (look->animation[0]) {
                l->crowd_animation = scene::find_animation(sh, look->animation);
                if (l->crowd_animation < 0) {
                    const char* hint = scene::suggest_animation(sh, look->animation);
                    diag_add(diags, kErrSpriteRef, s->path, field_line(crowd, kCrowdAnimationField, &sim::kCrowdSpriteDesc),
                             1, "entity '%s': sheet '%s' has no animation '%s'%s%s%s", crowd->name, sh->path,
                             look->animation, hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
                }
            }
        }
    }
    for (uint32_t i = 0; i < s->entity_count; ++i) add_record(l, &s->entities[i], nullptr, diags);
    l->generation = s->generation;
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
    bool ok = sync_records(l, diags);
    for (uint32_t i = 0; i < l->record_count; ++i) {
        SpriteRecord& r = l->records[i];
        if (strcmp(r.animation, r.sprite->animation) != 0) ok = start_animation(l, r, diags) && ok;
        set_frame(l, r);
        const uint32_t frames = l->sheets[r.sheet]->frame_count;
        if (r.sprite->frame >= frames) r.sprite->frame = frames - 1;  // a script set it past the end
    }
    return ok;
}

bool ensure_sheet_art(const scene::SpriteSheet* sheet, bool force, DiagList* diags, bool* cooked) {
    *cooked = false;
    if (!sheet->svg_count) return true;
#if ZARO_HAS_COOK
    char hash[17];
    if (!scene::svg_art_hash(sheet, cook::kCookVersion, hash)) {
        cook::check_svg_files(sheet, diags);  // says which file is missing
        return false;
    }
    char stored[17] = {};
    if (FILE* f = fopen(sheet->hash_path, "rb")) {
        const size_t n = fread(stored, 1, 16, f);
        stored[n] = 0;
        fclose(f);
    }
    if (!force && strcmp(stored, hash) == 0 && platform::file_mtime(sheet->image)) return true;
    uint8_t* atlas = cook::cook_svg_sheet(sheet, diags);
    if (!atlas) return false;
    char dir[260];
    snprintf(dir, sizeof(dir), "%s", sheet->image);
    if (char* slash = strrchr(dir, '/')) *slash = 0;
    bool ok = platform::create_directories(dir) &&
              write_png_rgba8(sheet->image, sheet->image_width, sheet->image_height, atlas);
    free(atlas);
    FILE* f = ok ? fopen(sheet->hash_path, "wb") : nullptr;
    ok = f && fwrite(hash, 1, 16, f) == 16;
    if (f) ok = fclose(f) == 0 && ok;
    if (!ok) {
        diag_add(diags, kErrIo, sheet->image, 0, 0, "cannot write the cooked sprite sheet");
        return false;
    }
    *cooked = true;
    return true;
#else
    (void)force;
    if (platform::file_mtime(sheet->image)) return true;
    diag_add(diags, kErrSpriteImage, sheet->path, sheet->svg_line, 1,
             "the SVG art is not cooked (%s is missing); cook it with the zaro tool: zaro cook %s", sheet->image,
             sheet->path);
    return false;
#endif
}

bool prepare_sprite_art(SpriteLayer* l, DiagList* diags) {
    bool ok = true;
    for (uint32_t i = 0; l && i < l->sheet_count; ++i) {
        bool cooked = false;
        ok = ensure_sheet_art(l->sheets[i], false, diags, &cooked) && ok;
        if (cooked) log_info("cooked %s (%u frames)", l->sheets[i]->image, l->sheets[i]->frame_count);
    }
    return ok;
}

uint32_t sprite_sheet_count(const SpriteLayer* l) { return l ? l->sheet_count : 0; }
const scene::SpriteSheet* sprite_sheet_at(const SpriteLayer* l, uint32_t i) { return l->sheets[i]; }

bool check_sprite_art(const SpriteLayer* l, DiagList* diags) {
    bool ok = true;
#if ZARO_HAS_COOK
    for (uint32_t i = 0; l && i < l->sheet_count; ++i) {
        if (l->sheets[i]->svg_count) ok = cook::check_svg_files(l->sheets[i], diags) && ok;
    }
#else
    (void)l;
    (void)diags;
#endif
    return ok;
}

namespace {

// Decodes sheet i's image and uploads it with its frames.
bool upload_sheet(SpriteLayer* l, uint32_t i) {
    const scene::SpriteSheet* sheet = l->sheets[i];
    uint8_t* rgba = nullptr;
    uint32_t w = 0, h = 0;
    if (!read_png_rgba8(sheet->image, &rgba, &w, &h)) {
        log_error(kErrSpriteImage, "%s: cannot read image '%s'", sheet->path, sheet->image);
        return false;
    }
    render::SpriteFrameInfo* frames = (render::SpriteFrameInfo*)malloc(sizeof(render::SpriteFrameInfo) * sheet->frame_count);
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
    return true;
}

// Uploads the sheets loaded since the last call and grows the instance arrays to the records.
bool ensure_gpu(SpriteLayer* l) {
    if (!l->renderer) return true;
    for (; l->gpu_sheet_count < l->sheet_count; ++l->gpu_sheet_count) {
        if (!upload_sheet(l, l->gpu_sheet_count)) return false;
    }
    if (l->record_count > l->instance_capacity) {
        uint32_t cap = l->instance_capacity ? l->instance_capacity : 64;
        while (cap < l->record_count) cap *= 2;
        l->instances = (render::SpriteInstance*)realloc(l->instances, sizeof(render::SpriteInstance) * cap);
        l->batches = (render::SpriteBatch*)realloc(l->batches, sizeof(render::SpriteBatch) * cap);
        l->instance_capacity = cap;
    }
    return true;
}

}  // namespace

bool attach_sprite_gpu(SpriteLayer* l, rhi::Device* device, const char* shader_dir) {
    l->device = device;
    if (!l->record_count && !l->spawnable) return true;  // only the crowd's look, which the crowd renderer draws
    l->renderer = render::create_sprite_renderer(device, shader_dir);
    if (!l->renderer) return false;
    return ensure_gpu(l);
}

bool load_crowd_sprites(const SpriteLayer* l, render::CrowdSprites* out) {
    *out = render::CrowdSprites{};
    if (!l || l->crowd_sheet < 0) return true;
    const scene::SpriteSheet* sheet = l->sheets[l->crowd_sheet];
    uint8_t* rgba = nullptr;
    uint32_t w = 0, h = 0;
    if (!read_png_rgba8(sheet->image, &rgba, &w, &h)) {
        log_error(kErrSpriteImage, "%s: cannot read image '%s'", sheet->path, sheet->image);
        return false;
    }
    const scene::SheetAnimation* a = l->crowd_animation >= 0 ? &sheet->animations[l->crowd_animation] : nullptr;
    const uint32_t n = a ? a->frame_count : sheet->frame_count;
    auto* frames = (render::SpriteFrameInfo*)malloc(sizeof(render::SpriteFrameInfo) * n);
    for (uint32_t i = 0; i < n; ++i) {
        const scene::SheetFrame& sf = sheet->frames[a ? a->frames[i] : i];
        frames[i] = {{(float)sf.x / (float)w, (float)sf.y / (float)h, (float)(sf.x + sf.w) / (float)w,
                      (float)(sf.y + sf.h) / (float)h},
                     {(float)sf.w, (float)sf.h},
                     {sf.pivot[0], sf.pivot[1]}};
    }
    out->rgba = rgba;
    out->width = w;
    out->height = h;
    out->frames = frames;
    out->frame_count = n;
    out->fps = a ? a->fps : 10.0f;
    out->metres_per_pixel = l->crowd_scale / sheet->pixels_per_unit;
    return true;
}

void free_crowd_sprites(render::CrowdSprites* c) {
    if (c->rgba) free_png((uint8_t*)c->rgba);
    free((void*)c->frames);
    *c = render::CrowdSprites{};
}

bool reload_sprite_layer_shaders(SpriteLayer* l) { return !l || !l->renderer || render::reload_sprite_shaders(l->renderer); }

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
