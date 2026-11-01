// UI on the GPU: a screen with a HUD bar, an image, a scroll list and a modal popup, in Korean and
// English. The frame is written as PNG and checked at a few pixels. Needs a GPU (label `gpu`).
// argv[1] is the build directory (holding shaders/ and fonts/), argv[2] the PNG to write.
#include "core/diag.h"
#include "core/png.h"
#include "render/ui_renderer.h"
#include "rhi/rhi.h"
#include "ui/context.h"
#include "ui/draw.h"
#include "ui/screen.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace zaro;

static_assert(sizeof(ui::Quad) == sizeof(render::UiQuad), "ui::Quad and render::UiQuad must match");

namespace {

constexpr uint32_t kWidth = 720, kHeight = 480;

int g_failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

template <typename T>
T* add(ui::Screen* s, int n, const reflect::ComponentDesc& desc) {
    return (T*)ui::add_widget(s, n, &desc);
}

ui::Layout* box(ui::Screen* s, int n, float w, float h) {
    ui::Layout* l = add<ui::Layout>(s, n, ui::kLayoutDesc);
    l->width = w;
    l->height = h;
    return l;
}

void set(char* dst, const char* text) { snprintf(dst, reflect::kStringCapacity, "%s", text); }

// A 64 x 64 PNG: an orange disc on transparent pixels.
bool write_test_image(const char* path) {
    uint8_t px[64 * 64 * 4];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const int dx = x - 32, dy = y - 32;
            const bool in = dx * dx + dy * dy < 30 * 30;
            uint8_t* p = px + (y * 64 + x) * 4;
            p[0] = 240, p[1] = 150, p[2] = 40, p[3] = in ? 255 : 0;
        }
    }
    return write_png_rgba8(path, 64, 64, px);
}

ui::Screen* build_screen(ui::Context* context, const char* image_path) {
    ui::Screen* s = ui::create_screen(context, "test");
    // HUD: a bar across the top with an icon, the gold amount and a settings button.
    const int bar = ui::add_node(s, "bar", ui::kNoNode);
    ui::Layout* l = box(s, bar, 0.0f, 64.0f);
    l->fill_width = true;
    l->direction = ui::kDirRow;
    l->align = ui::kAlignCenter;
    l->gap = 12.0f;
    l->padding[0] = l->padding[2] = 16.0f;
    add<ui::Panel>(s, bar, ui::kPanelDesc)->color = ui::rgba(20, 24, 32, 230);
    const int icon = ui::add_node(s, "icon", bar);
    box(s, icon, 40.0f, 40.0f);
    set(add<ui::Image>(s, icon, ui::kImageDesc)->path, image_path);
    const int gold = ui::add_node(s, "gold", bar);
    add<ui::Layout>(s, gold, ui::kLayoutDesc)->fill_width = true;
    ui::Label* lb = add<ui::Label>(s, gold, ui::kLabelDesc);
    set(lb->text, "\xea\xb3\xa8\xeb\x93\x9c 1,234");  // 골드 1,234
    lb->font[0] = 0;
    set(lb->font, "bold");
    const int settings = ui::add_node(s, "settings", bar);
    box(s, settings, 96.0f, 44.0f);
    ui::Button* b = add<ui::Button>(s, settings, ui::kButtonDesc);
    set(b->text, "\xec\x84\xa4\xec\xa0\x95");  // 설정
    set(b->action, "open_settings");
    b->size = 20.0f;

    // A list of rewards on the left, partly scrolled.
    const int list = ui::add_node(s, "list", ui::kNoNode);
    l = box(s, list, 260.0f, 380.0f);
    l->offset[0] = 16.0f;
    l->offset[1] = 80.0f;
    l->gap = 8.0f;
    add<ui::ScrollList>(s, list, ui::kScrollListDesc)->scroll = 30.0f;
    for (int i = 0; i < 8; ++i) {
        char name[16], text[64];
        snprintf(name, sizeof(name), "reward%d", i);
        snprintf(text, sizeof(text), "\xeb\xb3\xb4\xec\x83\x81 %d / Reward %d", i + 1, i + 1);  // 보상
        const int item = ui::add_node(s, name, list);
        l = box(s, item, 0.0f, 56.0f);
        l->fill_width = true;
        l->padding[0] = 14.0f;
        l->align = ui::kAlignCenter;
        l->direction = ui::kDirRow;
        ui::Panel* p = add<ui::Panel>(s, item, ui::kPanelDesc);
        p->color = ui::rgba(44, 52, 70);
        p->radius = 10.0f;
        const int label = ui::add_node(s, "label", item);
        lb = add<ui::Label>(s, label, ui::kLabelDesc);
        set(lb->text, text);
        lb->size = 18.0f;
    }

    // The settings popup: a modal with a centred panel, a title, a wrapped paragraph and two buttons.
    const int modal = ui::add_node(s, "popup", ui::kNoNode);
    add<ui::Modal>(s, modal, ui::kModalDesc);
    const int panel = ui::add_node(s, "panel", modal);
    l = box(s, panel, 360.0f, 0.0f);
    l->anchor[0] = 0.62f;
    l->anchor[1] = 0.5f;
    l->direction = ui::kDirColumn;
    l->padding[0] = l->padding[1] = l->padding[2] = l->padding[3] = 20.0f;
    l->gap = 14.0f;
    ui::Panel* p = add<ui::Panel>(s, panel, ui::kPanelDesc);
    p->color = ui::rgba(250, 248, 244);
    p->radius = 18.0f;
    const int title = ui::add_node(s, "title", panel);
    add<ui::Layout>(s, title, ui::kLayoutDesc)->fill_width = true;
    lb = add<ui::Label>(s, title, ui::kLabelDesc);
    set(lb->text, "\xec\x84\xa4\xec\xa0\x95 Settings");  // 설정 Settings
    set(lb->font, "bold");
    lb->size = 26.0f;
    lb->color = ui::rgba(30, 30, 36);
    lb->align = ui::kTextCenter;
    const int body = ui::add_node(s, "body", panel);
    add<ui::Layout>(s, body, ui::kLayoutDesc)->fill_width = true;
    lb = add<ui::Label>(s, body, ui::kLabelDesc);
    // 소리와 언어를 바꿀 수 있어요. Changes apply at once.
    set(lb->text, "\xec\x86\x8c\xeb\xa6\xac\xec\x99\x80 \xec\x96\xb8\xec\x96\xb4\xeb\xa5\xbc \xeb\xb0\x94\xea\xbf\x80 "
                  "\xec\x88\x98 \xec\x9e\x88\xec\x96\xb4\xec\x9a\x94. Changes apply at once.");
    lb->size = 18.0f;
    lb->wrap = true;
    lb->color = ui::rgba(70, 70, 80);
    const int buttons = ui::add_node(s, "buttons", panel);
    l = box(s, buttons, 0.0f, 48.0f);
    l->fill_width = true;
    l->direction = ui::kDirRow;
    l->gap = 12.0f;
    const char* captions[2] = {"\xec\xb7\xa8\xec\x86\x8c", "\xed\x99\x95\xec\x9d\xb8"};  // 취소, 확인
    for (int i = 0; i < 2; ++i) {
        const int n = ui::add_node(s, i ? "ok" : "cancel", buttons);
        l = add<ui::Layout>(s, n, ui::kLayoutDesc);
        l->fill_width = l->fill_height = true;
        b = add<ui::Button>(s, n, ui::kButtonDesc);
        set(b->text, captions[i]);
        set(b->action, i ? "ok" : "cancel");
        if (!i) b->color = ui::rgba(120, 124, 136);
    }
    ui::set_focus(s, ui::find_node(s, "ok"), true);
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: test_ui_render <build dir with trailing slash> <out.png>\n");
        return 2;
    }
    rhi::DeviceDesc dd;
    dd.width = kWidth;
    dd.height = kHeight;
    rhi::Device* device = rhi::create_device(dd);
    if (!device) return 1;
    char path[600];
    snprintf(path, sizeof(path), "%sshaders/", argv[1]);
    ui::ContextDesc cd;
    ui::Context* context = ui::create_context(cd);
    render::UiRenderer* renderer =
        render::create_ui_renderer(device, path, cd.glyph_atlas_size, cd.image_atlas_size);
    DiagList* diags = new DiagList();
    snprintf(path, sizeof(path), "%sfonts/NotoSansKR-Regular.otf", argv[1]);
    ui::add_font(context, "regular", path, diags);
    snprintf(path, sizeof(path), "%sfonts/NotoSansKR-Bold.otf", argv[1]);
    ui::add_font(context, "bold", path, diags);
    char image[600];
    snprintf(image, sizeof(image), "%sui_render_test_icon.png", argv[1]);
    if (!renderer || diags->count || !write_test_image(image)) {
        diag_print_text(diags, stderr);
        return 1;
    }

    ui::Screen* screen = build_screen(context, image);
    ui::DrawList list;
    ui::begin_frame(context);
    ui::update_screen(screen, ui::UiInput{}, (float)kWidth, (float)kHeight, 1.0f, 1.0f / 60.0f);
    ui::reset_draw_list(&list, (float)kWidth, (float)kHeight);
    ui::draw_screen(screen, &list);

    const ui::AtlasUpdate glyphs = ui::take_atlas_update(ui::context_text(context));
    const ui::AtlasUpdate images = ui::take_image_update(context);
    render::UiFrame frame;
    frame.quads = (const render::UiQuad*)list.quads;
    frame.quad_count = list.count;
    frame.atlas = glyphs.pixels;
    frame.atlas_size = glyphs.size;
    frame.atlas_x0 = glyphs.x0, frame.atlas_y0 = glyphs.y0, frame.atlas_x1 = glyphs.x1, frame.atlas_y1 = glyphs.y1;
    frame.images = images.pixels;
    frame.image_atlas_size = images.size;
    frame.images_x0 = images.x0, frame.images_y0 = images.y0, frame.images_x1 = images.x1, frame.images_y1 = images.y1;
    const float clear[4] = {0.10f, 0.32f, 0.20f, 1.0f};
    if (rhi::begin_frame(device)) {
        render::render_ui(renderer, frame, clear);
        rhi::end_frame(device);
    }
    rhi::wait_idle(device);
    uint8_t* px = (uint8_t*)malloc((size_t)kWidth * kHeight * 4);
    if (rhi::device_failed(device) || !rhi::capture_rgba8(device, px) || !write_png_rgba8(argv[2], kWidth, kHeight, px)) {
        fprintf(stderr, "FAIL: render or capture failed\n");
        ++g_failures;
    } else {
        auto at = [&](const char* node, float fx, float fy) {
            const ui::Rect r = ui::node_rect(screen, ui::find_node(screen, node));
            const uint32_t x = (uint32_t)(r.x0 + (r.x1 - r.x0) * fx), y = (uint32_t)(r.y0 + (r.y1 - r.y0) * fy);
            return px + ((size_t)y * kWidth + x) * 4;
        };
        const uint8_t* panel = at("panel", 0.03f, 0.5f);
        expect(panel[0] > 240 && panel[1] > 240, "the popup panel is light and not dimmed");
        const uint8_t* icon = at("icon", 0.5f, 0.5f);
        expect(icon[0] > 120 && icon[0] < 200 && icon[2] < 40, "the icon image shows (orange, dimmed by the modal)");
        const uint8_t* ok = at("ok", 0.1f, 0.5f);
        expect(ok[2] > 150 && ok[0] < 100, "the OK button is blue");
        const uint8_t* corner = px + ((size_t)(kHeight - 2) * kWidth + kWidth - 2) * 4;
        // Clear green g = 0.32 linear (154 sRGB); 60 % black on top leaves 0.128 linear (100 sRGB).
        expect(corner[1] > 90 && corner[1] < 110, "the background is dimmed by the modal");
        printf("ui render: %u quads, %u glyphs, wrote %s\n", list.count,
               ui::atlas_stats(ui::context_text(context)).glyphs, argv[2]);
    }
    free(px);
    ui::free_draw_list(&list);
    ui::destroy_screen(screen);
    delete diags;
    render::destroy_ui_renderer(renderer);
    ui::destroy_context(context);
    rhi::destroy_device(device);
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
