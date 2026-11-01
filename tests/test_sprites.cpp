// Sprite sheets: grid and explicit frames, pivots, animations on the tick clock (looping and held),
// and diagnostics with ids and lines. No GPU. Runs from the repository root (tests/data/sprites).
#include "core/diag.h"
#include "scene/sprite_sheet.h"

#include <stdio.h>
#include <string.h>

using namespace zaro;

namespace {

int g_failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

bool has_diag(const DiagList* d, ErrorId id, int line) {
    for (uint32_t i = 0; i < d->count; ++i) {
        if (d->items[i].id == id && d->items[i].line == line) return true;
    }
    return false;
}

void test_grid() {
    DiagList* d = new DiagList();
    scene::SpriteSheet* s = scene::load_sprite_sheet("tests/data/sprites/blob.toml", d);
    expect(s != nullptr && d->count == 0, "blob.toml loads");
    if (s) {
        expect(s->image_width == 128 && s->image_height == 32, "image size read from the PNG header");
        expect(s->frame_count == 4, "grid gives four frames");
        expect(s->frames[2].x == 64 && s->frames[2].w == 32 && s->frames[2].h == 32, "frame 2 rect");
        expect(s->pixels_per_unit == 32.0f, "pixels_per_unit");
        expect(s->animation_count == 2, "two animations");
        expect(strstr(s->image, "tests/data/sprites/blob.png") != nullptr, "image path resolved against the sheet");
        const int idle = scene::find_animation(s, "idle");
        const int land = scene::find_animation(s, "land");
        expect(idle == 0 && land == 1, "animations by name");
        expect(scene::find_animation(s, "walk") == -1, "unknown animation");
        const char* hint = scene::suggest_animation(s, "idel");
        expect(hint && strcmp(hint, "idle") == 0, "did you mean idle");
        // idle: 8 fps over frames 0..3, looping. One frame lasts 7.5 ticks.
        const scene::SheetAnimation& a = s->animations[idle];
        expect(scene::animation_frame(a, 0.0, 1.0f) == 0, "idle starts at frame 0");
        expect(scene::animation_frame(a, 0.13, 1.0f) == 1, "idle frame 1 at 0.13 s");
        expect(scene::animation_frame(a, 0.5, 1.0f) == 0, "idle loops after 4 frames");
        expect(scene::animation_frame(a, 0.13, 2.0f) == 2, "speed 2 plays twice as fast");
        expect(scene::animation_frame(a, 10.0, 0.0f) == 0, "speed 0 holds the first frame");
        // land: 3, 1, 0 at 10 fps, held at the end.
        const scene::SheetAnimation& b = s->animations[land];
        expect(scene::animation_frame(b, 0.15, 1.0f) == 1, "land second frame");
        expect(scene::animation_frame(b, 5.0, 1.0f) == 0, "land holds its last frame");
    }
    scene::free_sprite_sheet(s);
    delete d;
}

void test_frames() {
    DiagList* d = new DiagList();
    scene::SpriteSheet* s = scene::load_sprite_sheet("tests/data/sprites/frames.toml", d);
    expect(s != nullptr && d->count == 0, "frames.toml loads");
    if (s) {
        expect(s->frame_count == 2, "two explicit frames");
        expect(s->frames[0].pivot[0] == 0.5f && s->frames[0].pivot[1] == 1.0f, "default pivot");
        expect(s->frames[1].pivot[0] == 0.25f && s->frames[1].pivot[1] == 0.75f, "frame pivot");
        expect(s->pixels_per_unit == 100.0f, "default pixels_per_unit");
    }
    scene::free_sprite_sheet(s);
    delete d;
}

void test_errors() {
    DiagList* d = new DiagList();
    expect(scene::load_sprite_sheet("tests/data/sprites/errors.toml", d) == nullptr, "errors.toml fails");
    expect(has_diag(d, kErrSpriteSheet, 4), "pivot out of 0..1 at line 4");
    expect(has_diag(d, kErrSpriteSheet, 5), "unknown key at line 5");
    expect(has_diag(d, kErrSpriteImage, 7), "frame outside the image at line 7");
    expect(has_diag(d, kErrSpriteSheet, 12), "frame index out of range at line 12");
    if (g_failures) diag_print_text(d, stderr);
    d->count = 0;
    expect(scene::load_sprite_sheet("tests/data/sprites/missing_image.toml", d) == nullptr, "missing image fails");
    expect(has_diag(d, kErrSpriteImage, 2), "missing image at line 2");
    d->count = 0;
    expect(scene::load_sprite_sheet("tests/data/sprites/none.toml", d) == nullptr, "missing sheet fails");
    expect(d->count == 1 && d->items[0].id == kErrSpriteSheet, "missing sheet is ZE0900");
    delete d;
}

}  // namespace

int main() {
    test_grid();
    test_frames();
    test_errors();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("sprite sheets: ok\n");
    return 0;
}
