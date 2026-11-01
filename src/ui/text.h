// Text: fonts, line layout and the dynamic glyph atlas (D-019).
//
// Text is UTF-8. Glyphs are rasterized on first use into one single-channel atlas, keyed by
// font, glyph and pixel size, so Korean needs no prebaked character set. If the atlas fills up,
// glyphs that do not fit are skipped for the rest of the frame and begin_frame() clears the atlas
// before the next one, which then refills it with only what it draws.
//
// Measuring never touches the atlas, so layout is the same with or without a GPU.
#pragma once

#include "core/diag.h"
#include "ui/draw.h"

#include <stdint.h>

namespace zaro::ui {

struct TextSystem;

constexpr uint32_t kDefaultAtlasSize = 2048;
constexpr uint32_t kMaxFonts = 16;

TextSystem* create_text_system(uint32_t atlas_size = kDefaultAtlasSize);
void destroy_text_system(TextSystem* text);

// Loads a TrueType or OpenType file. Returns the font id (0, 1, ...) or -1 after adding a ZE0700
// diagnostic. Loading the same path twice returns the same id.
int load_font(TextSystem* text, const char* path, DiagList* diags);
uint32_t font_count(const TextSystem* text);

enum class Align : uint8_t { kLeft, kCenter, kRight };

struct TextStyle {
    int font = 0;
    float size = 16.0f;           // em size in pixels, rounded to whole pixels
    uint32_t color = rgba(255, 255, 255);
    float max_width = 0.0f;       // wrap lines longer than this; 0 never wraps
    float line_spacing = 1.0f;    // multiple of the font's own line height
    Align align = Align::kLeft;   // within max_width, or within the widest line when 0
};

struct TextSize {
    float width = 0.0f;   // widest line, trailing spaces excluded
    float height = 0.0f;  // lines * line height
    uint32_t lines = 0;
};

// Lines break at '\n' and, when max_width is set, after spaces. A word wider than max_width
// breaks between characters. Invalid UTF-8 shows as U+FFFD.
TextSize measure_text(TextSystem* text, const char* utf8, const TextStyle& style);
float line_height(TextSystem* text, const TextStyle& style);

// Appends one glyph quad per visible character, with the text box's top-left at (x, y).
TextSize draw_text(TextSystem* text, DrawList* list, float x, float y, const char* utf8, const TextStyle& style);

// Call once per frame before drawing text.
void begin_frame(TextSystem* text);

struct AtlasUpdate {
    const uint8_t* pixels = nullptr;  // size x size bytes, one row after another
    uint32_t size = 0;
    uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // region changed since the last call; empty if x1 <= x0
};
// Returns the atlas and the region to upload, then marks it clean.
AtlasUpdate take_atlas_update(TextSystem* text);

struct AtlasStats {
    uint32_t glyphs = 0;   // glyphs in the atlas now
    uint32_t resets = 0;   // times the atlas was cleared because it was full
    float used = 0.0f;     // fraction of rows allocated, 0..1
};
AtlasStats atlas_stats(const TextSystem* text);

// Decodes one UTF-8 codepoint and advances *p. Returns 0 at the end of the string.
uint32_t next_codepoint(const char** p);

}  // namespace zaro::ui
