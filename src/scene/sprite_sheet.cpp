#include "scene/sprite_sheet.h"

#include "core/hash.h"
#include "core/path.h"
#include "core/png.h"
#include "core/reflect.h"

#include <tomlc17.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

namespace {

bool is_sheet_number(const toml_datum_t& v) { return v.type == TOML_INT64 || v.type == TOML_FP64; }
double sheet_number(const toml_datum_t& v) { return v.type == TOML_INT64 ? (double)v.u.int64 : v.u.fp64; }

// Reads an array of exactly n numbers, each at least min. False (with a diagnostic) otherwise.
bool read_numbers(const toml_datum_t& v, uint32_t n, double min, double* out, const char* what, const char* file,
                  DiagList* d) {
    bool ok = v.type == TOML_ARRAY && (uint32_t)v.u.arr.size == n;
    for (uint32_t i = 0; ok && i < n; ++i) {
        ok = is_sheet_number(v.u.arr.elem[i]) && sheet_number(v.u.arr.elem[i]) >= min;
        if (ok) out[i] = sheet_number(v.u.arr.elem[i]);
    }
    if (!ok) {
        diag_add(d, kErrSpriteSheet, file, v.lineno, v.colno, "%s must be an array of %u numbers, each at least %g",
                 what, n, min);
    }
    return ok;
}

bool read_pivot(const toml_datum_t& v, float* pivot, const char* file, DiagList* d) {
    double p[2];
    if (!read_numbers(v, 2, 0.0, p, "pivot", file, d)) return false;
    if (p[0] > 1.0 || p[1] > 1.0) {
        diag_add(d, kErrSpriteSheet, file, v.lineno, v.colno, "pivot must be [x, y] with both in 0..1");
        return false;
    }
    pivot[0] = (float)p[0];
    pivot[1] = (float)p[1];
    return true;
}

void load_svg_list(SpriteSheet* s, const toml_datum_t& v, DiagList* d) {
    if (v.type != TOML_ARRAY || v.u.arr.size < 1 || (uint32_t)v.u.arr.size > kMaxSvgFrames) {
        diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno, "svg must be an array of 1 to %u SVG paths",
                 kMaxSvgFrames);
        return;
    }
    s->svg_line = v.lineno;
    for (int32_t i = 0; i < v.u.arr.size; ++i) {
        const toml_datum_t& e = v.u.arr.elem[i];
        if (e.type != TOML_STRING || !e.u.str.ptr[0]) {
            diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno, "svg[%d] must be a path", i);
            continue;
        }
        path_join_relative(s->path, e.u.str.ptr, s->svg[s->svg_count++], sizeof(s->svg[0]));
    }
}

// SVG sheets: the cooked image's place and size, and one frame per SVG file in a padded grid.
void layout_svg_frames(SpriteSheet* s) {
    char dir[260];
    snprintf(dir, sizeof(dir), "%s", s->path);
    char* slash = strrchr(dir, '/');
    const char* file = slash ? slash + 1 : dir;
    char stem[128];
    snprintf(stem, sizeof(stem), "%s", file);
    if (char* dot = strstr(stem, ".toml")) *dot = 0;
    if (slash) slash[1] = 0;
    else dir[0] = 0;
    snprintf(s->image, sizeof(s->image), "%s.zaro-cache/%s.png", dir, stem);
    snprintf(s->hash_path, sizeof(s->hash_path), "%s.zaro-cache/%s.hash", dir, stem);
    // One transparent pixel around each frame, so filtering never reads a neighbour.
    const uint32_t cw = s->frame_size[0] + 2, ch = s->frame_size[1] + 2;
    uint32_t cols = 1;
    while (cols * cols < s->svg_count) ++cols;
    const uint32_t rows = (s->svg_count + cols - 1) / cols;
    s->image_width = cols * cw;
    s->image_height = rows * ch;
    for (uint32_t i = 0; i < s->svg_count; ++i) {
        SheetFrame& f = s->frames[s->frame_count++];
        f.x = (i % cols) * cw + 1;
        f.y = (i / cols) * ch + 1;
        f.w = s->frame_size[0];
        f.h = s->frame_size[1];
        f.pivot[0] = s->pivot[0];
        f.pivot[1] = s->pivot[1];
    }
}

void load_header(SpriteSheet* s, const toml_datum_t& h, double* cell, int* cell_line, int* image_line, DiagList* d) {
    for (int32_t i = 0; i < h.u.tab.size; ++i) {
        const char* key = h.u.tab.key[i];
        const toml_datum_t& v = h.u.tab.value[i];
        if (strcmp(key, "image") == 0) {
            if (v.type != TOML_STRING || !v.u.str.ptr[0]) {
                diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno, "image must be a PNG path");
                continue;
            }
            path_join_relative(s->path, v.u.str.ptr, s->image, sizeof(s->image));
            *image_line = v.lineno;
        } else if (strcmp(key, "pixels_per_unit") == 0) {
            if (!is_sheet_number(v) || sheet_number(v) <= 0.0) {
                diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno, "pixels_per_unit must be a number above 0");
                continue;
            }
            s->pixels_per_unit = (float)sheet_number(v);
        } else if (strcmp(key, "pivot") == 0) {
            read_pivot(v, s->pivot, s->path, d);
        } else if (strcmp(key, "cell") == 0) {
            if (read_numbers(v, 2, 1.0, cell, "cell", s->path, d)) *cell_line = v.lineno;
        } else if (strcmp(key, "svg") == 0) {
            load_svg_list(s, v, d);
        } else if (strcmp(key, "size") == 0) {
            double size[2];
            if (!read_numbers(v, 2, 1.0, size, "size", s->path, d)) continue;
            if (size[0] > 2048.0 || size[1] > 2048.0) {
                diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno, "size must be at most 2048 x 2048 pixels");
                continue;
            }
            s->frame_size[0] = (uint32_t)size[0];
            s->frame_size[1] = (uint32_t)size[1];
        } else {
            diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno,
                     "unexpected key '%s' in [sheet] (expected image or svg, pixels_per_unit, pivot, cell, size)",
                     key);
        }
    }
}

void load_frames(SpriteSheet* s, const toml_datum_t& frames, DiagList* d) {
    for (int32_t i = 0; i < frames.u.arr.size; ++i) {
        const toml_datum_t& t = frames.u.arr.elem[i];
        if (t.type != TOML_TABLE) continue;
        if (s->frame_count == kMaxSheetFrames) {
            diag_add(d, kErrSpriteSheet, s->path, t.lineno, t.colno, "a sheet holds at most %u frames", kMaxSheetFrames);
            return;
        }
        SheetFrame& f = s->frames[s->frame_count++];
        f.pivot[0] = s->pivot[0];
        f.pivot[1] = s->pivot[1];
        bool has_rect = false;
        for (int32_t k = 0; k < t.u.tab.size; ++k) {
            const char* key = t.u.tab.key[k];
            const toml_datum_t& v = t.u.tab.value[k];
            if (strcmp(key, "rect") == 0) {
                double r[4];
                if (!read_numbers(v, 4, 0.0, r, "rect", s->path, d)) continue;
                if (r[2] < 1.0 || r[3] < 1.0) {
                    diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno, "rect width and height must be at least 1");
                    continue;
                }
                f.x = (uint32_t)r[0], f.y = (uint32_t)r[1], f.w = (uint32_t)r[2], f.h = (uint32_t)r[3];
                has_rect = true;
            } else if (strcmp(key, "pivot") == 0) {
                read_pivot(v, f.pivot, s->path, d);
            } else if (strcmp(key, "name") != 0) {
                diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno,
                         "unexpected key '%s' in [[frame]] (expected rect, pivot or name)", key);
            }
        }
        if (!has_rect) diag_add(d, kErrSpriteSheet, s->path, t.lineno, t.colno, "[[frame]] needs rect = [x, y, w, h]");
        else if (s->image_width && (f.x + f.w > s->image_width || f.y + f.h > s->image_height)) {
            diag_add(d, kErrSpriteImage, s->path, t.lineno, t.colno, "frame %u (%u, %u, %ux%u) is outside the %ux%u image",
                     s->frame_count - 1, f.x, f.y, f.w, f.h, s->image_width, s->image_height);
        }
    }
}

void load_animations(SpriteSheet* s, const toml_datum_t& anims, DiagList* d) {
    for (int32_t i = 0; i < anims.u.arr.size; ++i) {
        const toml_datum_t& t = anims.u.arr.elem[i];
        if (t.type != TOML_TABLE) continue;
        if (s->animation_count == kMaxSheetAnimations) {
            diag_add(d, kErrSpriteSheet, s->path, t.lineno, t.colno, "a sheet holds at most %u animations",
                     kMaxSheetAnimations);
            return;
        }
        SheetAnimation& a = s->animations[s->animation_count];
        memset(&a, 0, sizeof(a));
        a.fps = 10.0f;
        a.loop = true;
        bool has_frames = false;
        for (int32_t k = 0; k < t.u.tab.size; ++k) {
            const char* key = t.u.tab.key[k];
            const toml_datum_t& v = t.u.tab.value[k];
            if (strcmp(key, "name") == 0) {
                if (v.type != TOML_STRING || !v.u.str.ptr[0] || strlen(v.u.str.ptr) >= sizeof(a.name)) {
                    diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno, "name must be a string of 1 to %u bytes",
                             (unsigned)sizeof(a.name) - 1);
                } else {
                    snprintf(a.name, sizeof(a.name), "%s", v.u.str.ptr);
                }
            } else if (strcmp(key, "frames") == 0) {
                if (v.type != TOML_ARRAY || v.u.arr.size < 1 || (uint32_t)v.u.arr.size > kMaxAnimationFrames) {
                    diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno, "frames must be an array of 1 to %u indices",
                             kMaxAnimationFrames);
                    continue;
                }
                has_frames = true;
                for (int32_t f = 0; f < v.u.arr.size; ++f) {
                    const toml_datum_t& e = v.u.arr.elem[f];
                    if (e.type != TOML_INT64 || e.u.int64 < 0 || (uint64_t)e.u.int64 >= s->frame_count) {
                        diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno,
                                 "frames[%d] must be a frame index from 0 to %u", f,
                                 s->frame_count ? s->frame_count - 1 : 0);
                        continue;
                    }
                    a.frames[a.frame_count++] = (uint16_t)e.u.int64;
                }
            } else if (strcmp(key, "fps") == 0) {
                if (!is_sheet_number(v) || sheet_number(v) <= 0.0 || sheet_number(v) > 240.0) {
                    diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno, "fps must be a number above 0 and at most 240");
                } else {
                    a.fps = (float)sheet_number(v);
                }
            } else if (strcmp(key, "loop") == 0) {
                if (v.type == TOML_BOOLEAN) a.loop = v.u.boolean;
                else diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno, "loop must be true or false");
            } else {
                diag_add(d, kErrSpriteSheet, s->path, v.lineno, v.colno,
                         "unexpected key '%s' in [[animation]] (expected name, frames, fps or loop)", key);
            }
        }
        if (!a.name[0] || !has_frames) {
            diag_add(d, kErrSpriteSheet, s->path, t.lineno, t.colno, "[[animation]] needs a name and frames");
            continue;
        }
        if (find_animation(s, a.name) >= 0) {
            diag_add(d, kErrSpriteSheet, s->path, t.lineno, t.colno, "two animations are named '%s'", a.name);
            continue;
        }
        ++s->animation_count;
    }
}

}  // namespace

SpriteSheet* load_sprite_sheet(const char* path, DiagList* d) {
    SpriteSheet* s = (SpriteSheet*)calloc(1, sizeof(SpriteSheet));
    snprintf(s->path, sizeof(s->path), "%s", path);
    s->pixels_per_unit = 100.0f;
    s->pivot[0] = 0.5f;
    s->pivot[1] = 1.0f;
    FILE* f = fopen(path, "rb");
    if (!f) {
        diag_add(d, kErrSpriteSheet, path, 0, 0, "cannot open sprite sheet");
        free(s);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)size + 1);
    const size_t n = fread(text, 1, (size_t)size, f);
    text[n] = 0;
    fclose(f);
    const uint32_t before = d->count;
    toml_result_t r = toml_parse_named(text, (int)n, path);
    if (!r.ok) {
        diag_add(d, kErrSceneSyntax, path, 0, 0, "invalid TOML: %s", r.errmsg);
    } else {
        const toml_datum_t top = r.toptab;
        const toml_datum_t header = toml_get(top, "sheet");
        double cell[2] = {0.0, 0.0};
        int cell_line = 0, image_line = 0;
        for (int32_t i = 0; i < top.u.tab.size; ++i) {
            const char* key = top.u.tab.key[i];
            if (strcmp(key, "sheet") != 0 && strcmp(key, "frame") != 0 && strcmp(key, "animation") != 0) {
                diag_add(d, kErrSpriteSheet, path, top.u.tab.value[i].lineno, top.u.tab.value[i].colno,
                         "unexpected top-level key '%s' (expected [sheet], [[frame]] or [[animation]])", key);
            }
        }
        if (header.type != TOML_TABLE) {
            diag_add(d, kErrSpriteSheet, path, 1, 1, "a sprite sheet needs a [sheet] table");
        } else {
            load_header(s, header, cell, &cell_line, &image_line, d);
            if (s->svg_count || s->svg_line) {
                if (s->image[0] || cell_line || toml_get(top, "frame").type == TOML_ARRAY) {
                    diag_add(d, kErrSpriteSheet, path, s->svg_line, 1,
                             "an SVG sheet takes its frames from svg; it has no image, cell or [[frame]]");
                } else if (!s->frame_size[0]) {
                    diag_add(d, kErrSpriteSheet, path, s->svg_line, 1, "an SVG sheet needs size = [width, height]");
                } else {
                    layout_svg_frames(s);
                }
            } else if (!s->image[0]) {
                diag_add(d, kErrSpriteSheet, path, header.lineno, header.colno, "[sheet] needs image = \"<file>.png\"");
            } else if (!png_size(s->image, &s->image_width, &s->image_height)) {
                diag_add(d, kErrSpriteImage, path, image_line, 1, "cannot open image '%s' (missing, or not a PNG file)",
                         s->image);
            }
        }
        const toml_datum_t frames = toml_get(top, "frame");
        if (s->svg_count) {
            // Frames come from the SVG files (layout_svg_frames).
        } else if (cell_line && frames.type == TOML_ARRAY) {
            diag_add(d, kErrSpriteSheet, path, cell_line, 1, "a sheet has either cell or [[frame]] tables, not both");
        } else if (cell_line && s->image_width) {
            // Grid: every whole cell of the image, row by row.
            const uint32_t cw = (uint32_t)cell[0], ch = (uint32_t)cell[1];
            const uint32_t cols = s->image_width / cw, rows = s->image_height / ch;
            if (!cols || !rows) {
                diag_add(d, kErrSpriteImage, path, cell_line, 1, "cell %ux%u is larger than the %ux%u image", cw, ch,
                         s->image_width, s->image_height);
            }
            for (uint32_t y = 0; y < rows; ++y) {
                for (uint32_t x = 0; x < cols && s->frame_count < kMaxSheetFrames; ++x) {
                    SheetFrame& fr = s->frames[s->frame_count++];
                    fr.x = x * cw, fr.y = y * ch, fr.w = cw, fr.h = ch;
                    fr.pivot[0] = s->pivot[0];
                    fr.pivot[1] = s->pivot[1];
                }
            }
        } else if (frames.type == TOML_ARRAY) {
            load_frames(s, frames, d);
        } else if (!cell_line && header.type == TOML_TABLE && !s->svg_line) {
            diag_add(d, kErrSpriteSheet, path, header.lineno, header.colno,
                     "a sheet needs cell = [w, h] in [sheet] or [[frame]] tables");
        }
        const toml_datum_t anims = toml_get(top, "animation");
        if (anims.type == TOML_ARRAY) load_animations(s, anims, d);
    }
    if (r.ok || r.__internal) toml_free(r);
    free(text);
    if (d->count != before) {
        free(s);
        return nullptr;
    }
    return s;
}

void free_sprite_sheet(SpriteSheet* s) { free(s); }

int find_animation(const SpriteSheet* s, const char* name) {
    for (uint32_t i = 0; i < s->animation_count; ++i) {
        if (strcmp(s->animations[i].name, name) == 0) return (int)i;
    }
    return -1;
}

const char* suggest_animation(const SpriteSheet* s, const char* name) {
    const char* names[kMaxSheetAnimations];
    for (uint32_t i = 0; i < s->animation_count; ++i) names[i] = s->animations[i].name;
    return reflect::suggest_name(name, names, s->animation_count);
}

bool svg_art_hash(const SpriteSheet* s, uint32_t cook_version, char out[17]) {
    uint64_t h = fnv1a(&cook_version, sizeof(cook_version));
    for (uint32_t i = 0; i <= s->svg_count; ++i) {
        FILE* f = fopen(i == 0 ? s->path : s->svg[i - 1], "rb");
        if (!f) return false;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) h = fnv1a(buf, n, h);
        fclose(f);
        h = fnv1a(&i, sizeof(i), h);  // file boundaries count
    }
    snprintf(out, 17, "%016llx", (unsigned long long)h);
    return true;
}

uint32_t animation_frame(const SheetAnimation& a, double seconds, float speed) {
    const double t = seconds * (double)a.fps * (double)speed;
    uint64_t step = t > 0.0 ? (uint64_t)floor(t) : 0;
    if (a.loop) step %= a.frame_count;
    else if (step >= a.frame_count) step = a.frame_count - 1;
    return a.frames[step];
}

}  // namespace zaro::scene
