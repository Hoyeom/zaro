// UI renderer: draws screen-space quads (shapes and glyphs) over the frame in one pass and one
// draw call. It takes plain data; the ui module builds it (D-014).
#pragma once

#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro::render {

// 80 bytes. Mirrors ui::Quad and `struct Quad` in shaders/ui.slang.
struct UiQuad {
    float rect[4];  // x0, y0, x1, y1 in pixels, origin top-left
    float uv[4];    // glyph atlas u0, v0, u1, v1
    float clip[4];  // x0, y0, x1, y1
    uint32_t color;  // sRGB RGBA8, red in the low byte
    uint32_t border_color;
    uint32_t kind;  // 0 shape, 1 glyph
    float radius;
    float border;
    float pad[3];
};
static_assert(sizeof(UiQuad) == 80, "must match shaders/ui.slang");

struct UiFrame {
    const UiQuad* quads = nullptr;
    uint32_t quad_count = 0;
    // The glyph atlas (atlas_size squared, one byte per texel) and the region that changed since
    // the previous frame, uploaded before drawing. Nothing is uploaded when x1 <= x0.
    const uint8_t* atlas = nullptr;
    uint32_t atlas_size = 0;
    uint32_t atlas_x0 = 0, atlas_y0 = 0, atlas_x1 = 0, atlas_y1 = 0;
};

struct UiRenderer;

// shader_dir holds ui.wgsl. atlas_size must match the UiFrame atlas.
UiRenderer* create_ui_renderer(rhi::Device* device, const char* shader_dir, uint32_t atlas_size);
void destroy_ui_renderer(UiRenderer* renderer);
bool reload_ui_shaders(UiRenderer* renderer);

// Records the "ui" pass. clear_rgba nullptr draws over what earlier passes left. Call between
// rhi::begin_frame and rhi::end_frame.
void render_ui(UiRenderer* renderer, const UiFrame& frame, const float* clear_rgba);

}  // namespace zaro::render
