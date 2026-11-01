// UI draw list: screen-space quads in pixels (origin top-left, y down), drawn in order.
// render::UiRenderer draws the quads as they are; it never sees ui types (D-014).
#pragma once

#include <stdint.h>

namespace zaro::ui {

enum QuadKind : uint32_t {
    kQuadShape = 0,  // filled rectangle with optional rounded corners and border
    kQuadGlyph = 1,  // coverage from the glyph atlas, tinted with color
    kQuadImage = 2,  // colour from the image atlas, multiplied by color
};

// 80 bytes. Mirrors render::UiQuad and `struct Quad` in shaders/ui.slang; keep all three in step.
struct Quad {
    float rect[4];  // x0, y0, x1, y1 in pixels
    float uv[4];    // glyph or image atlas u0, v0, u1, v1 (normalized)
    float clip[4];  // x0, y0, x1, y1; pixels outside are not drawn
    uint32_t color;         // sRGB RGBA8, red in the low byte, straight (not premultiplied) alpha
    uint32_t border_color;  // shapes only
    uint32_t kind;          // QuadKind
    float radius;           // shapes: corner radius in pixels
    float border;           // shapes: border width in pixels, inside the rectangle
    float pad[3];
};
static_assert(sizeof(Quad) == 80, "must match render::UiQuad and shaders/ui.slang");

constexpr uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) {
    return (r & 255u) | (g & 255u) << 8 | (b & 255u) << 16 | (a & 255u) << 24;
}

struct DrawList {
    Quad* quads = nullptr;
    uint32_t count = 0;
    uint32_t capacity = 0;
    float clip[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // applied to every quad pushed
};

// Empties the list and sets the clip to the whole width x height target.
void reset_draw_list(DrawList* list, float width, float height);
void free_draw_list(DrawList* list);
// Appends a quad with the current clip and everything else zero. The pointer is valid until the
// next push.
Quad* push_quad(DrawList* list);
void draw_rect(DrawList* list, float x0, float y0, float x1, float y1, uint32_t color, float radius = 0.0f,
               float border = 0.0f, uint32_t border_color = 0);

}  // namespace zaro::ui
