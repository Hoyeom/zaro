#include "ui/font.h"

#include <stdio.h>
#include <stdlib.h>

// stb_truetype is third-party C written for other warning levels; compile it quietly.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4244 4245 4456 4457 4701 4702 4703)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

namespace zaro::ui {

struct FontFace {
    unsigned char* data = nullptr;  // stb_truetype reads from this for the face's lifetime
    stbtt_fontinfo info;
};

FontFace* font_load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* data = size > 0 ? (unsigned char*)malloc((size_t)size) : nullptr;
    const size_t read = data ? fread(data, 1, (size_t)size, f) : 0;
    fclose(f);
    const int offset = data && read == (size_t)size ? stbtt_GetFontOffsetForIndex(data, 0) : -1;
    FontFace* face = new FontFace();
    if (offset < 0 || !stbtt_InitFont(&face->info, data, offset)) {
        free(data);
        delete face;
        return nullptr;
    }
    face->data = data;
    return face;
}

void font_free(FontFace* face) {
    if (!face) return;
    free(face->data);
    delete face;
}

float font_scale(const FontFace* face, float em_pixels) {
    return stbtt_ScaleForMappingEmToPixels(&face->info, em_pixels);
}

void font_vmetrics(const FontFace* face, int* ascent, int* descent, int* line_gap) {
    stbtt_GetFontVMetrics(&face->info, ascent, descent, line_gap);
}

int font_glyph(const FontFace* face, uint32_t codepoint) { return stbtt_FindGlyphIndex(&face->info, (int)codepoint); }

int font_advance(const FontFace* face, int glyph) {
    int advance = 0, bearing = 0;
    stbtt_GetGlyphHMetrics(&face->info, glyph, &advance, &bearing);
    return advance;
}

int font_kern(const FontFace* face, int left, int right) {
    return stbtt_GetGlyphKernAdvance(&face->info, left, right);
}

void font_glyph_box(const FontFace* face, int glyph, float scale, int* x0, int* y0, int* x1, int* y1) {
    stbtt_GetGlyphBitmapBox(&face->info, glyph, scale, scale, x0, y0, x1, y1);
}

void font_render(const FontFace* face, int glyph, float scale, uint8_t* out, int w, int h, int stride) {
    stbtt_MakeGlyphBitmap(&face->info, out, w, h, stride, scale, scale, glyph);
}

}  // namespace zaro::ui
