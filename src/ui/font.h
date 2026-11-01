// Font faces through stb_truetype (D-019). font.cpp is the only file that includes
// stb_truetype.h, so swapping the rasterizer (e.g. for FreeType) stays inside it.
// Internal to the ui module; text.h is the interface the rest of the engine uses.
#pragma once

#include <stdint.h>

namespace zaro::ui {

struct FontFace;

// Reads a TrueType or OpenType (CFF) file; the first face of a collection. nullptr if the file is
// missing or not a font.
FontFace* font_load(const char* path);
void font_free(FontFace* face);

// Pixels per font unit for an em size in pixels.
float font_scale(const FontFace* face, float em_pixels);
// Ascent (positive, above the baseline), descent (negative) and line gap, in font units.
void font_vmetrics(const FontFace* face, int* ascent, int* descent, int* line_gap);
// Glyph index for a Unicode codepoint; 0 (the .notdef box) when the font lacks it.
int font_glyph(const FontFace* face, uint32_t codepoint);
// Horizontal advance and kerning between two glyphs, in font units.
int font_advance(const FontFace* face, int glyph);
int font_kern(const FontFace* face, int left, int right);
// Bitmap box of the glyph at `scale`, relative to the pen position on the baseline (y down).
// Empty (x1 <= x0) for blank glyphs such as the space.
void font_glyph_box(const FontFace* face, int glyph, float scale, int* x0, int* y0, int* x1, int* y1);
// Rasterizes 8-bit coverage into a w x h region whose rows are `stride` bytes apart.
void font_render(const FontFace* face, int glyph, float scale, uint8_t* out, int w, int h, int stride);

}  // namespace zaro::ui
