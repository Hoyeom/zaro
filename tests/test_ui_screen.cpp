// Screens without a GPU: layout (anchors, rows, columns, fill, padding, scale), clicks, drag and
// wheel scrolling, modal input blocking, focus navigation, and the color/enum reflection types.
// argv[1] is the fonts directory.
#include "core/diag.h"
#include "core/reflect.h"
#include "ui/context.h"
#include "ui/screen.h"
#include "ui/widgets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace zaro;

namespace {

int g_failures = 0;
ui::Context* g_context = nullptr;

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

bool near(float a, float b) { return fabsf(a - b) < 0.01f; }

bool rect_is(ui::Rect r, float x0, float y0, float x1, float y1) {
    const bool ok = near(r.x0, x0) && near(r.y0, y0) && near(r.x1, x1) && near(r.y1, y1);
    if (!ok) fprintf(stderr, "  rect is %.2f %.2f %.2f %.2f\n", r.x0, r.y0, r.x1, r.y1);
    return ok;
}

ui::Layout* layout(ui::Screen* s, int n) { return (ui::Layout*)ui::add_widget(s, n, &ui::kLayoutDesc); }

ui::Button* button(ui::Screen* s, int n, const char* action) {
    ui::Button* b = (ui::Button*)ui::add_widget(s, n, &ui::kButtonDesc);
    snprintf(b->text, sizeof(b->text), "%s", action);
    snprintf(b->action, sizeof(b->action), "%s", action);
    return b;
}

void frame(ui::Screen* s, const ui::UiInput& in = ui::UiInput{}, float scale = 1.0f) {
    ui::update_screen(s, in, 800.0f, 600.0f, scale, 1.0f / 60.0f);
}

ui::UiInput pointer(float x, float y, bool down, bool pressed, bool released) {
    ui::UiInput in;
    in.pointer_x = x;
    in.pointer_y = y;
    in.pointer_down = down;
    in.pointer_pressed = pressed;
    in.pointer_released = released;
    return in;
}

void click(ui::Screen* s, float x, float y) {
    frame(s, pointer(x, y, true, true, false));
    frame(s, pointer(x, y, false, false, true));
}

bool clicked(const ui::Screen* s, const char* action) {
    for (uint32_t i = 0; i < ui::event_count(s); ++i) {
        const ui::Event* e = ui::event_at(s, i);
        if (e->kind == ui::EventKind::kClick && strcmp(e->action, action) == 0) return true;
    }
    return false;
}

void test_reflection_types() {
    const reflect::FieldDesc* color = reflect::find_field(&ui::kPanelDesc, "color");
    const reflect::FieldDesc* dir = reflect::find_field(&ui::kLayoutDesc, "direction");
    uint32_t v = 0;
    char err[300], text[128];
    expect(reflect::parse_text_value(*color, "#ff8000", &v, err, sizeof(err)) && v == 0xff0080ffu, "#ff8000 parses");
    expect(reflect::parse_text_value(*color, "#FF800040", &v, err, sizeof(err)) && v == 0x400080ffu, "alpha parses");
    reflect::format_text_value(*color, &v, text, sizeof(text));
    expect(strcmp(text, "#ff800040") == 0, "colours format as #rrggbbaa");
    expect(!reflect::parse_text_value(*color, "ff8000", &v, err, sizeof(err)), "a colour needs #");
    expect(!reflect::parse_text_value(*color, "#ff80zz", &v, err, sizeof(err)), "a colour needs hex digits");
    expect(reflect::parse_text_value(*dir, "column", &v, err, sizeof(err)) && v == ui::kDirColumn, "enum parses");
    expect(!reflect::parse_text_value(*dir, "colum", &v, err, sizeof(err)) && strstr(err, "did you mean 'column'"),
           "a misspelt enum suggests the closest choice");
    reflect::format_text_value(*dir, &v, text, sizeof(text));
    expect(strcmp(text, "column") == 0 || v != ui::kDirColumn, "enums format as their name");
}

void test_layout() {
    ui::Screen* s = ui::create_screen(g_context, "layout");
    // A 200 x 100 panel anchored bottom-right, moved 10 left.
    const int corner = ui::add_node(s, "corner", ui::kNoNode);
    ui::Layout* l = layout(s, corner);
    l->width = 200.0f;
    l->height = 100.0f;
    l->anchor[0] = l->anchor[1] = 1.0f;
    l->offset[0] = -10.0f;
    // A full-width top bar: a row with padding, a fixed box, a filling box and another fixed box.
    const int bar = ui::add_node(s, "bar", ui::kNoNode);
    l = layout(s, bar);
    l->fill_width = true;
    l->height = 60.0f;
    l->direction = ui::kDirRow;
    l->padding[0] = l->padding[1] = l->padding[2] = l->padding[3] = 10.0f;
    l->gap = 5.0f;
    l->align = ui::kAlignCenter;
    const int a = ui::add_node(s, "a", bar);
    layout(s, a)->width = 50.0f;
    layout(s, a)->height = 20.0f;
    const int fill = ui::add_node(s, "fill", bar);
    layout(s, fill)->fill_width = true;
    layout(s, fill)->fill_height = true;
    const int b = ui::add_node(s, "b", bar);
    layout(s, b)->width = 30.0f;
    layout(s, b)->height = 40.0f;
    // A centred column that fits its children.
    const int col = ui::add_node(s, "col", ui::kNoNode);
    l = layout(s, col);
    l->direction = ui::kDirColumn;
    l->anchor[0] = l->anchor[1] = 0.5f;
    l->gap = 10.0f;
    l->align = ui::kAlignCenter;
    const int c1 = ui::add_node(s, "c1", col);
    layout(s, c1)->width = 100.0f;
    layout(s, c1)->height = 50.0f;
    const int c2 = ui::add_node(s, "c2", col);
    layout(s, c2)->width = 60.0f;
    layout(s, c2)->height = 30.0f;

    frame(s);
    expect(rect_is(ui::node_rect(s, corner), 590.0f, 500.0f, 790.0f, 600.0f), "anchored panel");
    expect(rect_is(ui::node_rect(s, bar), 0.0f, 0.0f, 800.0f, 60.0f), "fill_width bar");
    expect(rect_is(ui::node_rect(s, a), 10.0f, 20.0f, 60.0f, 40.0f), "row child, centred across");
    expect(rect_is(ui::node_rect(s, fill), 65.0f, 10.0f, 755.0f, 50.0f), "fill child takes the free width and height");
    expect(rect_is(ui::node_rect(s, b), 760.0f, 10.0f, 790.0f, 50.0f), "last row child at the end");
    expect(rect_is(ui::node_rect(s, col), 350.0f, 255.0f, 450.0f, 345.0f), "column fits and centres");
    expect(rect_is(ui::node_rect(s, c2), 370.0f, 315.0f, 430.0f, 345.0f), "column child centred across");
    // Scale doubles every design unit.
    frame(s, ui::UiInput{}, 2.0f);
    expect(rect_is(ui::node_rect(s, corner), 380.0f, 400.0f, 780.0f, 600.0f), "scale applies to size and offset");
    // Hidden nodes leave the layout.
    layout(s, a)->visible = false;
    frame(s);
    expect(!ui::node_shown(s, a) && rect_is(ui::node_rect(s, fill), 10.0f, 10.0f, 755.0f, 50.0f),
           "a hidden child gives its space to the others");
    ui::destroy_screen(s);
}

void test_text_sizes() {
    ui::Screen* s = ui::create_screen(g_context, "text");
    const int label = ui::add_node(s, "label", ui::kNoNode);
    ui::Label* lb = (ui::Label*)ui::add_widget(s, label, &ui::kLabelDesc);
    snprintf(lb->text, sizeof(lb->text), "%s", "\xec\x95\x88\xeb\x85\x95\xed\x95\x98\xec\x84\xb8\xec\x9a\x94");  // 안녕하세요
    lb->size = 20.0f;
    frame(s);
    const ui::Rect r = ui::node_rect(s, label);
    // Noto Sans KR advances Hangul by 0.92 em: five syllables at 20 px are 92 px.
    expect(r.x1 - r.x0 > 90.0f && r.x1 - r.x0 < 94.0f, "a label fits its text: five 20 px syllables");
    lb->wrap = true;
    layout(s, label)->width = 50.0f;
    frame(s);
    const ui::Rect w = ui::node_rect(s, label);
    expect(w.y1 - w.y0 > 2.5f * (r.y1 - r.y0), "a wrapping label grows to three lines in 50 units");
    ui::destroy_screen(s);
}

void test_clicks_and_modal() {
    ui::Screen* s = ui::create_screen(g_context, "clicks");
    const int ok = ui::add_node(s, "ok", ui::kNoNode);
    ui::Layout* l = layout(s, ok);
    l->width = 100.0f;
    l->height = 50.0f;
    button(s, ok, "ok");
    const int off = ui::add_node(s, "off", ui::kNoNode);
    l = layout(s, off);
    l->width = 100.0f;
    l->height = 50.0f;
    l->offset[0] = 200.0f;
    button(s, off, "off")->enabled = false;
    frame(s);

    click(s, 50.0f, 25.0f);
    expect(clicked(s, "ok"), "press and release on a button clicks it");
    click(s, 250.0f, 25.0f);
    expect(ui::event_count(s) == 0, "a disabled button does not click");
    frame(s, pointer(50.0f, 25.0f, true, true, false));
    frame(s, pointer(150.0f, 25.0f, false, false, true));
    expect(ui::event_count(s) == 0, "releasing outside the button cancels the click");
    frame(s, pointer(50.0f, 25.0f, false, false, false));
    expect(ui::hovered_node(s) == ok && ui::pointer_captured(s), "a mouse over a button hovers it");
    ui::UiInput touch = pointer(50.0f, 25.0f, false, false, false);
    touch.pointer_touch = true;
    frame(s, touch);
    expect(ui::hovered_node(s) == ui::kNoNode, "a finger that is not down hovers nothing");

    // A modal (with a button inside) blocks the buttons behind it.
    const int modal = ui::add_node(s, "modal", ui::kNoNode);
    ui::add_widget(s, modal, &ui::kModalDesc);
    const int close = ui::add_node(s, "close", modal);
    l = layout(s, close);
    l->width = 100.0f;
    l->height = 50.0f;
    l->anchor[0] = l->anchor[1] = 0.5f;
    button(s, close, "close");
    frame(s);
    click(s, 50.0f, 25.0f);
    expect(ui::event_count(s) == 0, "a modal blocks the buttons behind it");
    expect(ui::pointer_captured(s), "a modal captures the pointer everywhere");
    click(s, 400.0f, 300.0f);
    expect(clicked(s, "close"), "a button inside the modal clicks");
    layout(s, modal)->visible = false;
    frame(s);
    click(s, 50.0f, 25.0f);
    expect(clicked(s, "ok"), "hiding the modal gives input back");
    ui::destroy_screen(s);
}

void test_scroll_list() {
    ui::Screen* s = ui::create_screen(g_context, "scroll");
    const int list = ui::add_node(s, "list", ui::kNoNode);
    ui::Layout* l = layout(s, list);
    l->width = 200.0f;
    l->height = 300.0f;
    l->gap = 10.0f;
    l->offset[1] = 100.0f;  // the list spans y 100..400
    ui::ScrollList* sl = (ui::ScrollList*)ui::add_widget(s, list, &ui::kScrollListDesc);
    int items[10];
    for (int i = 0; i < 10; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "item%d", i);
        items[i] = ui::add_node(s, name, list);
        layout(s, items[i])->fill_width = true;
        layout(s, items[i])->height = 90.0f;
        button(s, items[i], name);
    }
    frame(s);
    expect(rect_is(ui::node_rect(s, items[1]), 0.0f, 200.0f, 200.0f, 290.0f), "list items stack with the gap");
    // Content is 10 x 90 + 9 x 10 = 990 high in a 300 list: at most 690 of scroll.
    frame(s, pointer(100.0f, 350.0f, true, true, false));
    frame(s, pointer(100.0f, 300.0f, true, false, false));
    frame(s, pointer(100.0f, 250.0f, true, false, false));
    frame(s, pointer(100.0f, 250.0f, false, false, true));
    expect(ui::event_count(s) == 0, "dragging a list does not click the item under the finger");
    expect(sl->scroll >= 99.0f, "dragging up by 100 scrolls by at least 100 (plus fling)");
    for (int i = 0; i < 120; ++i) frame(s);
    const float settled = sl->scroll;
    frame(s);
    expect(near(settled, sl->scroll), "fling comes to rest");
    ui::UiInput wheel = pointer(100.0f, 200.0f, false, false, false);
    wheel.wheel_y = -100.0f;
    frame(s, wheel);
    frame(s);
    expect(near(sl->scroll, 690.0f), "the wheel scrolls, clamped to the content end");
    expect(rect_is(ui::node_rect(s, items[9]), 0.0f, 310.0f, 200.0f, 400.0f), "the last item ends at the list end");
    click(s, 100.0f, 50.0f);  // item6 lies at y 10..100, above the list
    expect(ui::event_count(s) == 0, "items scrolled out of the list do not click");
    ui::destroy_screen(s);
}

void test_focus_navigation() {
    ui::Screen* s = ui::create_screen(g_context, "focus");
    // A 2 x 2 grid of buttons.
    const char* names[4] = {"tl", "tr", "bl", "br"};
    int nodes[4];
    for (int i = 0; i < 4; ++i) {
        nodes[i] = ui::add_node(s, names[i], ui::kNoNode);
        ui::Layout* l = layout(s, nodes[i]);
        l->width = 100.0f;
        l->height = 50.0f;
        l->offset[0] = (float)(i % 2) * 200.0f;
        l->offset[1] = (float)(i / 2) * 100.0f;
        button(s, nodes[i], names[i]);
    }
    frame(s);
    ui::UiInput nav;
    nav.nav_down = true;
    frame(s, nav);
    expect(ui::focused_node(s) == ui::kNoNode, "a screen that is not navigable takes no focus");
    ui::set_navigable(s, true);
    frame(s, nav);
    expect(ui::focused_node(s) == nodes[0] && ui::focus_visible(s), "the first navigation focuses the first button");
    frame(s, nav);
    expect(ui::focused_node(s) == nodes[2], "down moves to the button below");
    nav = ui::UiInput{};
    nav.nav_right = true;
    frame(s, nav);
    expect(ui::focused_node(s) == nodes[3], "right moves to the button beside");
    frame(s, nav);
    expect(ui::focused_node(s) == nodes[3], "nothing further right keeps the focus");
    nav = ui::UiInput{};
    nav.nav_confirm = true;
    frame(s, nav);
    expect(clicked(s, "br"), "confirm clicks the focused button");
    frame(s, pointer(700.0f, 500.0f, true, true, false));
    expect(!ui::focus_visible(s), "a pointer press hides the focus ring");
    frame(s, pointer(700.0f, 500.0f, false, false, true));
    nav = ui::UiInput{};
    nav.nav_confirm = true;
    frame(s, nav);
    expect(ui::event_count(s) == 0, "confirm does nothing while the focus ring is hidden");
    nav = ui::UiInput{};
    nav.nav_back = true;
    frame(s, nav);
    expect(ui::event_count(s) == 1 && ui::event_at(s, 0)->kind == ui::EventKind::kBack, "back is reported");

    // An open modal scopes navigation to its own buttons.
    const int modal = ui::add_node(s, "modal", ui::kNoNode);
    ui::add_widget(s, modal, &ui::kModalDesc);
    const int yes = ui::add_node(s, "yes", modal);
    layout(s, yes)->width = 80.0f;
    layout(s, yes)->height = 40.0f;
    button(s, yes, "yes");
    frame(s);
    nav = ui::UiInput{};
    nav.nav_left = true;
    frame(s, nav);
    expect(ui::focused_node(s) == yes, "inside a modal the focus moves to the modal's buttons");
    ui::destroy_screen(s);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: test_ui_screen <fonts dir with trailing slash>\n");
        return 2;
    }
    g_context = ui::create_context();
    DiagList* diags = new DiagList();
    char path[512];
    snprintf(path, sizeof(path), "%sNotoSansKR-Regular.otf", argv[1]);
    ui::add_font(g_context, "regular", path, diags);
    snprintf(path, sizeof(path), "%sNotoSansKR-Bold.otf", argv[1]);
    ui::add_font(g_context, "bold", path, diags);
    if (diags->count) {
        diag_print_text(diags, stderr);
        return 1;
    }
    test_reflection_types();
    test_layout();
    test_text_sizes();
    test_clicks_and_modal();
    test_scroll_list();
    test_focus_navigation();
    delete diags;
    ui::destroy_context(g_context);
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    puts("ui screen: all checks passed");
    return 0;
}
