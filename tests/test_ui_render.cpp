// Text and shapes on the GPU: draws a panel with Korean and English text offscreen, writes the
// frame as PNG and checks that glyph pixels landed where the text is. Needs a GPU (label `gpu`).
// argv[1] is the build directory (holding shaders/ and fonts/), argv[2] the PNG to write.
#include "core/diag.h"
#include "core/png.h"
#include "render/ui_renderer.h"
#include "rhi/rhi.h"
#include "ui/draw.h"
#include "ui/text.h"

#include <stdio.h>
#include <stdlib.h>

static_assert(sizeof(zaro::ui::Quad) == sizeof(zaro::render::UiQuad), "ui::Quad and render::UiQuad must match");

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: test_ui_render <build dir with trailing slash> <out.png>\n");
        return 2;
    }
    constexpr uint32_t kWidth = 640, kHeight = 240;
    zaro::rhi::DeviceDesc dd;
    dd.width = kWidth;
    dd.height = kHeight;
    zaro::rhi::Device* device = zaro::rhi::create_device(dd);
    if (!device) return 1;
    char path[600];
    snprintf(path, sizeof(path), "%sshaders/", argv[1]);
    zaro::ui::TextSystem* text = zaro::ui::create_text_system();
    zaro::render::UiRenderer* renderer = zaro::render::create_ui_renderer(device, path, zaro::ui::kDefaultAtlasSize);
    zaro::DiagList* diags = new zaro::DiagList();
    snprintf(path, sizeof(path), "%sfonts/NotoSansKR-Regular.otf", argv[1]);
    const int regular = zaro::ui::load_font(text, path, diags);
    snprintf(path, sizeof(path), "%sfonts/NotoSansKR-Bold.otf", argv[1]);
    const int bold = zaro::ui::load_font(text, path, diags);
    if (!renderer || regular < 0 || bold < 0) {
        zaro::diag_print_text(diags, stderr);
        return 1;
    }

    zaro::ui::DrawList list;
    zaro::ui::reset_draw_list(&list, (float)kWidth, (float)kHeight);
    zaro::ui::begin_frame(text);
    zaro::ui::draw_rect(&list, 20.0f, 20.0f, 620.0f, 220.0f, zaro::ui::rgba(32, 36, 48), 16.0f, 2.0f,
                        zaro::ui::rgba(120, 140, 200));
    zaro::ui::TextStyle title;
    title.font = bold;
    title.size = 36.0f;
    title.max_width = 560.0f;
    title.align = zaro::ui::Align::kCenter;
    // 안녕하세요, ZARO!
    zaro::ui::draw_text(text, &list, 40.0f, 40.0f,
                        "\xec\x95\x88\xeb\x85\x95\xed\x95\x98\xec\x84\xb8\xec\x9a\x94, ZARO!", title);
    zaro::ui::TextStyle body;
    body.font = regular;
    body.size = 20.0f;
    body.max_width = 560.0f;
    body.color = zaro::ui::rgba(220, 225, 235);
    // 한글과 English가 한 줄에서 자연스럽게 섞이고, 긴 문장은 너비에 맞춰 줄이 바뀝니다.
    zaro::ui::draw_text(text, &list, 40.0f, 110.0f,
                        "\xed\x95\x9c\xea\xb8\x80\xea\xb3\xbc English\xea\xb0\x80 \xed\x95\x9c \xec\xa4\x84\xec\x97\x90\xec\x84\x9c "
                        "\xec\x9e\x90\xec\x97\xb0\xec\x8a\xa4\xeb\x9f\xbd\xea\xb2\x8c \xec\x84\x9e\xec\x9d\xb4\xea\xb3\xa0, "
                        "\xea\xb8\xb4 \xeb\xac\xb8\xec\x9e\xa5\xec\x9d\x80 \xeb\x84\x88\xeb\xb9\x84\xec\x97\x90 "
                        "\xeb\xa7\x9e\xec\xb6\xb0 \xec\xa4\x84\xec\x9d\xb4 \xeb\xb0\x94\xeb\x80\x9d\xeb\x8b\x88\xeb\x8b\xa4.",
                        body);

    const zaro::ui::AtlasUpdate u = zaro::ui::take_atlas_update(text);
    zaro::render::UiFrame frame;
    frame.quads = (const zaro::render::UiQuad*)list.quads;
    frame.quad_count = list.count;
    frame.atlas = u.pixels;
    frame.atlas_size = u.size;
    frame.atlas_x0 = u.x0;
    frame.atlas_y0 = u.y0;
    frame.atlas_x1 = u.x1;
    frame.atlas_y1 = u.y1;
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int code = 0;
    if (zaro::rhi::begin_frame(device)) {
        zaro::render::render_ui(renderer, frame, clear);
        zaro::rhi::end_frame(device);
    }
    zaro::rhi::wait_idle(device);
    uint8_t* pixels = (uint8_t*)malloc((size_t)kWidth * kHeight * 4);
    if (zaro::rhi::device_failed(device) || !zaro::rhi::capture_rgba8(device, pixels) ||
        !zaro::write_png_rgba8(argv[2], kWidth, kHeight, pixels)) {
        fprintf(stderr, "FAIL: render or capture failed\n");
        code = 1;
    } else {
        // White title glyphs on the dark panel, and nothing but the clear colour outside it.
        uint32_t bright = 0;
        for (uint32_t y = 40; y < 100; ++y) {
            for (uint32_t x = 40; x < 600; ++x) bright += pixels[(y * kWidth + x) * 4] > 200 ? 1 : 0;
        }
        const uint8_t* corner = pixels;
        const uint8_t* panel = pixels + (200 * kWidth + 320) * 4;
        if (bright < 500) {
            fprintf(stderr, "FAIL: title has only %u bright pixels\n", bright);
            code = 1;
        }
        if (corner[0] != 0 || corner[1] != 0 || corner[2] != 0) {
            fprintf(stderr, "FAIL: outside the panel is not the clear colour\n");
            code = 1;
        }
        if (panel[0] < 20 || panel[0] > 50 || panel[2] < 35 || panel[2] > 70) {
            fprintf(stderr, "FAIL: panel colour is %u %u %u\n", panel[0], panel[1], panel[2]);
            code = 1;
        }
        printf("ui render: %u quads, %u atlas glyphs, wrote %s\n", list.count, zaro::ui::atlas_stats(text).glyphs,
               argv[2]);
    }
    free(pixels);
    zaro::ui::free_draw_list(&list);
    delete diags;
    zaro::render::destroy_ui_renderer(renderer);
    zaro::ui::destroy_text_system(text);
    zaro::rhi::destroy_device(device);
    return code;
}
