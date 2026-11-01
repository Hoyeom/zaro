#include "ui/text.h"

#include "ui/font.h"
#include "ui/pack.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::ui {

namespace {

constexpr uint32_t kCacheCapacity = 1u << 14;              // glyph cache slots, a power of two
constexpr uint32_t kMaxCached = kCacheCapacity / 10 * 7;  // keep probing chains short
constexpr uint32_t kReplacement = 0xFFFD;

}  // namespace

// Not in the anonymous namespace: TextSystem (external linkage) holds these, and GCC rejects that
// with -Wsubobject-linkage.
struct GlyphEntry {
    uint64_t key;  // 0 = empty slot
    uint16_t x, y, w, h;
    int16_t xoff, yoff;
};

struct TextLine {
    const char* begin;
    const char* end;
    float width;
};

struct LoadedFont {
    FontFace* face;
    char path[260];
    int ascent, descent, line_gap;
};

struct TextSystem {
    LoadedFont fonts[kMaxFonts];
    uint32_t font_count = 0;

    uint32_t size = 0;
    uint8_t* pixels = nullptr;
    ShelfPacker packer;
    GlyphEntry* cache = nullptr;
    uint32_t cached = 0;
    uint32_t dirty[4] = {0, 0, 0, 0};
    bool full = false;  // a glyph did not fit this frame; begin_frame clears the atlas
    uint32_t resets = 0;

    TextLine* lines = nullptr;
    uint32_t line_capacity = 0;
};

namespace {

int pixel_size(float size) {
    const int px = (int)(size + 0.5f);
    return px < 1 ? 1 : (px > 1024 ? 1024 : px);
}

uint64_t hash64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return x;
}

void mark_dirty(TextSystem* t, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    if (t->dirty[2] <= t->dirty[0]) {
        t->dirty[0] = x0;
        t->dirty[1] = y0;
        t->dirty[2] = x1;
        t->dirty[3] = y1;
        return;
    }
    if (x0 < t->dirty[0]) t->dirty[0] = x0;
    if (y0 < t->dirty[1]) t->dirty[1] = y0;
    if (x1 > t->dirty[2]) t->dirty[2] = x1;
    if (y1 > t->dirty[3]) t->dirty[3] = y1;
}

void clear_atlas(TextSystem* t) {
    memset(t->pixels, 0, (size_t)t->size * t->size);
    memset(t->cache, 0, sizeof(GlyphEntry) * kCacheCapacity);
    t->cached = 0;
    pack_reset(&t->packer, t->size);
    t->full = false;
    mark_dirty(t, 0, 0, t->size, t->size);
}

// The atlas entry for a glyph, rasterizing it on first use. nullptr if the atlas is full.
const GlyphEntry* find_glyph(TextSystem* t, uint32_t font, int glyph, int px, float scale) {
    const uint64_t key = 1ull << 63 | (uint64_t)font << 48 | (uint64_t)px << 32 | (uint32_t)glyph;
    uint32_t slot = (uint32_t)hash64(key) & (kCacheCapacity - 1);
    while (t->cache[slot].key) {
        if (t->cache[slot].key == key) return &t->cache[slot];
        slot = (slot + 1) & (kCacheCapacity - 1);
    }
    if (t->full || t->cached >= kMaxCached) {
        t->full = true;
        return nullptr;
    }
    const FontFace* face = t->fonts[font].face;
    int x0, y0, x1, y1;
    font_glyph_box(face, glyph, scale, &x0, &y0, &x1, &y1);
    GlyphEntry e = {key, 0, 0, 0, 0, (int16_t)x0, (int16_t)y0};
    if (x1 > x0 && y1 > y0) {
        uint32_t ax, ay;
        const uint32_t w = (uint32_t)(x1 - x0), h = (uint32_t)(y1 - y0);
        if (!pack(&t->packer, w, h, &ax, &ay)) {
            t->full = true;
            return nullptr;
        }
        font_render(face, glyph, scale, t->pixels + (size_t)ay * t->size + ax, (int)w, (int)h, (int)t->size);
        mark_dirty(t, ax, ay, ax + w, ay + h);
        e.x = (uint16_t)ax;
        e.y = (uint16_t)ay;
        e.w = (uint16_t)w;
        e.h = (uint16_t)h;
    }
    t->cache[slot] = e;
    t->cached++;
    return &t->cache[slot];
}

const LoadedFont* style_font(const TextSystem* t, const TextStyle& style, uint32_t* index) {
    if (!t->font_count) return nullptr;
    *index = style.font >= 0 && (uint32_t)style.font < t->font_count ? (uint32_t)style.font : 0;
    return &t->fonts[*index];
}

bool is_space(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == 0x3000; }

void push_line(TextSystem* t, uint32_t* count, const char* begin, const char* end, float width) {
    if (*count == t->line_capacity) {
        t->line_capacity = t->line_capacity ? t->line_capacity * 2 : 64;
        t->lines = (TextLine*)realloc(t->lines, sizeof(TextLine) * t->line_capacity);
    }
    t->lines[(*count)++] = TextLine{begin, end, width};
}

// Splits the text into t->lines. Returns the line count.
uint32_t layout_lines(TextSystem* t, const LoadedFont& f, const char* text, float scale, float max_width) {
    uint32_t count = 0;
    if (!text || !*text) return 0;
    const char* line_start = text;
    const char* after_space = nullptr;  // where the line may break: the start of the last word
    float pen = 0.0f, visible = 0.0f, visible_at_break = 0.0f;
    int prev = -1;
    const char* p = text;
    for (;;) {
        const char* at = p;
        const uint32_t cp = next_codepoint(&p);
        if (cp == 0 || cp == '\n') {
            push_line(t, &count, line_start, at, visible);
            if (cp == 0) break;
            line_start = p;
            after_space = nullptr;
            pen = visible = 0.0f;
            prev = -1;
            continue;
        }
        const int glyph = font_glyph(f.face, cp);
        float advance = (float)font_advance(f.face, glyph) * scale;
        if (prev >= 0) advance += (float)font_kern(f.face, prev, glyph) * scale;
        if (is_space(cp)) {
            pen += advance;
            after_space = p;
            visible_at_break = visible;
            prev = glyph;
            continue;
        }
        if (max_width > 0.0f && pen > 0.0f && pen + advance > max_width) {
            // Break after the last space, or else before this character; then rescan from there.
            const char* next = after_space && after_space > line_start ? after_space : at;
            push_line(t, &count, line_start, next, next == at ? visible : visible_at_break);
            line_start = p = next;
            after_space = nullptr;
            pen = visible = 0.0f;
            prev = -1;
            continue;
        }
        pen += advance;
        visible = pen;
        prev = glyph;
    }
    return count;
}

float font_line_height(const LoadedFont& f, float scale) {
    return (float)(f.ascent - f.descent + f.line_gap) * scale;
}

}  // namespace

TextSystem* create_text_system(uint32_t atlas_size) {
    TextSystem* t = new TextSystem();
    t->size = atlas_size;
    t->pixels = (uint8_t*)malloc((size_t)atlas_size * atlas_size);
    t->cache = (GlyphEntry*)malloc(sizeof(GlyphEntry) * kCacheCapacity);
    clear_atlas(t);
    return t;
}

void destroy_text_system(TextSystem* t) {
    if (!t) return;
    for (uint32_t i = 0; i < t->font_count; ++i) font_free(t->fonts[i].face);
    free(t->pixels);
    free(t->cache);
    free(t->lines);
    delete t;
}

int load_font(TextSystem* t, const char* path, DiagList* diags) {
    for (uint32_t i = 0; i < t->font_count; ++i) {
        if (strcmp(t->fonts[i].path, path) == 0) return (int)i;
    }
    if (t->font_count == kMaxFonts) {
        diag_add(diags, kErrFontLoad, path, 0, 0, "cannot load more than %u fonts", kMaxFonts);
        return -1;
    }
    FontFace* face = font_load(path);
    if (!face) {
        diag_add(diags, kErrFontLoad, path, 0, 0, "cannot load font (missing, or not a TrueType/OpenType file)");
        return -1;
    }
    LoadedFont& f = t->fonts[t->font_count];
    f.face = face;
    snprintf(f.path, sizeof(f.path), "%s", path);
    font_vmetrics(face, &f.ascent, &f.descent, &f.line_gap);
    return (int)t->font_count++;
}

uint32_t font_count(const TextSystem* t) { return t->font_count; }

float line_height(TextSystem* t, const TextStyle& style) {
    uint32_t index;
    const LoadedFont* f = style_font(t, style, &index);
    if (!f) return 0.0f;
    return font_line_height(*f, font_scale(f->face, (float)pixel_size(style.size))) * style.line_spacing;
}

TextSize measure_text(TextSystem* t, const char* utf8, const TextStyle& style) {
    TextSize size;
    uint32_t index;
    const LoadedFont* f = style_font(t, style, &index);
    if (!f) return size;
    const float scale = font_scale(f->face, (float)pixel_size(style.size));
    size.lines = layout_lines(t, *f, utf8, scale, style.max_width);
    for (uint32_t i = 0; i < size.lines; ++i) {
        if (t->lines[i].width > size.width) size.width = t->lines[i].width;
    }
    size.height = (float)size.lines * font_line_height(*f, scale) * style.line_spacing;
    return size;
}

TextSize draw_text(TextSystem* t, DrawList* list, float x, float y, const char* utf8, const TextStyle& style) {
    TextSize size;
    uint32_t index;
    const LoadedFont* f = style_font(t, style, &index);
    if (!f) return size;
    const int px = pixel_size(style.size);
    const float scale = font_scale(f->face, (float)px);
    size.lines = layout_lines(t, *f, utf8, scale, style.max_width);
    for (uint32_t i = 0; i < size.lines; ++i) {
        if (t->lines[i].width > size.width) size.width = t->lines[i].width;
    }
    const float lh = font_line_height(*f, scale) * style.line_spacing;
    size.height = (float)size.lines * lh;
    const float box = style.max_width > 0.0f ? style.max_width : size.width;
    // The font's own line box, centred in the (possibly stretched) line.
    const float baseline_offset = (lh - font_line_height(*f, scale)) * 0.5f + (float)f->ascent * scale;
    const float inv = 1.0f / (float)t->size;
    for (uint32_t i = 0; i < size.lines; ++i) {
        const TextLine& line = t->lines[i];
        float lx = x;
        if (style.align == Align::kCenter) lx += (box - line.width) * 0.5f;
        if (style.align == Align::kRight) lx += box - line.width;
        const float baseline = floorf(y + (float)i * lh + baseline_offset + 0.5f);
        float pen = 0.0f;
        int prev = -1;
        const char* p = line.begin;
        while (p < line.end) {
            const uint32_t cp = next_codepoint(&p);
            const int glyph = font_glyph(f->face, cp);
            if (prev >= 0) pen += (float)font_kern(f->face, prev, glyph) * scale;
            prev = glyph;
            const GlyphEntry* e = find_glyph(t, index, glyph, px, scale);
            if (e && e->w) {
                Quad* q = push_quad(list);
                q->rect[0] = floorf(lx + pen + 0.5f) + (float)e->xoff;
                q->rect[1] = baseline + (float)e->yoff;
                q->rect[2] = q->rect[0] + (float)e->w;
                q->rect[3] = q->rect[1] + (float)e->h;
                q->uv[0] = (float)e->x * inv;
                q->uv[1] = (float)e->y * inv;
                q->uv[2] = (float)(e->x + e->w) * inv;
                q->uv[3] = (float)(e->y + e->h) * inv;
                q->color = style.color;
                q->kind = kQuadGlyph;
            }
            pen += (float)font_advance(f->face, glyph) * scale;
        }
    }
    return size;
}

void begin_frame(TextSystem* t) {
    if (!t->full) return;
    clear_atlas(t);
    t->resets++;
}

AtlasUpdate take_atlas_update(TextSystem* t) {
    AtlasUpdate u;
    u.pixels = t->pixels;
    u.size = t->size;
    u.x0 = t->dirty[0];
    u.y0 = t->dirty[1];
    u.x1 = t->dirty[2];
    u.y1 = t->dirty[3];
    t->dirty[0] = t->dirty[1] = t->dirty[2] = t->dirty[3] = 0;
    return u;
}

AtlasStats atlas_stats(const TextSystem* t) {
    AtlasStats s;
    s.glyphs = t->cached;
    s.resets = t->resets;
    s.used = t->size ? (float)t->packer.next_y / (float)t->size : 0.0f;
    return s;
}

uint32_t next_codepoint(const char** p) {
    const unsigned char* s = (const unsigned char*)*p;
    const uint32_t c = s[0];
    if (c == 0) return 0;
    uint32_t len, cp, min;
    if (c < 0x80) {
        *p += 1;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        len = 2, cp = c & 0x1F, min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3, cp = c & 0x0F, min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4, cp = c & 0x07, min = 0x10000;
    } else {
        *p += 1;
        return kReplacement;
    }
    for (uint32_t i = 1; i < len; ++i) {
        if ((s[i] & 0xC0) != 0x80) {  // truncated sequence: skip only the bytes read so far
            *p += i;
            return kReplacement;
        }
        cp = cp << 6 | (s[i] & 0x3F);
    }
    *p += len;
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return kReplacement;
    return cp;
}

}  // namespace zaro::ui
