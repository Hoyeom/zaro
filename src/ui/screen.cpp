#include "ui/screen.h"

#include "core/log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::ui {

namespace {

constexpr uint32_t kMaxEvents = 64;
constexpr int kImageUnresolved = -2;
constexpr float kDragThreshold = 10.0f;  // design units the pointer moves before a press becomes a drag
constexpr float kWheelStep = 48.0f;      // design units per wheel step
constexpr float kFlingDecay = 5.0f;      // velocity falls by e every 1/5 s
constexpr float kFocusRingGap = 3.0f;
constexpr float kFocusRingWidth = 2.0f;
constexpr uint32_t kFocusRingColor = rgba(255, 255, 255);

const Layout kDefaultLayout{};

}  // namespace

struct Node {
    char name[64];
    int parent, first_child, last_child, next_sibling;
    void* widgets[kWidgetTypes];
    uint32_t owned;  // bit per widget type: allocated by the screen
    int image;       // image id, kImageUnresolved until first used, -1 if it failed to load
    char image_path[reflect::kStringCapacity];
    float pref_w, pref_h;
    Rect rect;
    Rect clip;            // the part of the screen where the node may draw (scroll lists clip)
    float content_main;   // scroll lists: stacked content length in pixels
    bool shown;
    uint32_t order;       // index in the frame's draw order
    uint32_t order_end;   // one past the node's last descendant in the draw order
};

struct Screen {
    Context* context = nullptr;
    char name[64] = {};
    char base_dir[260] = {};
    Node* nodes = nullptr;
    uint32_t count = 0;
    uint32_t capacity = 0;
    int first_root = kNoNode, last_root = kNoNode;
    bool navigable = false;
    float reference[2] = {1280.0f, 720.0f};

    float width = 0.0f, height = 0.0f, scale = 1.0f;
    int* order = nullptr;  // shown nodes in draw order
    uint32_t order_count = 0;
    int top_modal = kNoNode;
    bool laid_out = false;

    int hover = kNoNode;
    int pressed = kNoNode;
    int focus = kNoNode;
    bool focus_visible = false;
    bool pointer_down = false;
    bool captured = false;
    int drag_list = kNoNode;
    bool dragging = false;
    float drag_start[2] = {0.0f, 0.0f};
    float last_pointer[2] = {0.0f, 0.0f};
    float velocity = 0.0f;  // pixels per second along the dragged list's axis
    int fling_list = kNoNode;

    Event events[kMaxEvents];
    uint32_t event_count = 0;
};

namespace {

Node& node(Screen* s, int n) { return s->nodes[n]; }
const Node& node(const Screen* s, int n) { return s->nodes[n]; }

template <typename T>
T* widget(const Screen* s, int n, WidgetType type) {
    return (T*)s->nodes[n].widgets[type];
}

const Layout& layout_of(const Screen* s, int n) {
    const Layout* l = widget<Layout>(s, n, kWLayout);
    return l ? *l : kDefaultLayout;
}

Rect intersect(const Rect& a, const Rect& b) {
    Rect r{a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1};
    if (r.x1 < r.x0) r.x1 = r.x0;
    if (r.y1 < r.y0) r.y1 = r.y0;
    return r;
}

bool contains(const Rect& r, float x, float y) { return x >= r.x0 && y >= r.y0 && x < r.x1 && y < r.y1; }

TextStyle text_style(const Screen* s, const char* font, float size, uint32_t color) {
    TextStyle st;
    const int id = find_font(s->context, font);
    st.font = id >= 0 ? id : 0;
    st.size = size * s->scale;
    st.color = color;
    return st;
}

TextStyle label_style(const Screen* s, const Label& l) {
    TextStyle st = text_style(s, l.font, l.size, l.color);
    st.line_spacing = l.line_spacing;
    st.align = l.align == kTextCenter ? Align::kCenter : (l.align == kTextRight ? Align::kRight : Align::kLeft);
    return st;
}

// Resolves and caches the node's image; reloads when the path changes.
int node_image(Screen* s, int n) {
    Node& nd = node(s, n);
    const Image* img = widget<Image>(s, n, kWImage);
    if (!img || !img->path[0]) return -1;
    if (nd.image != kImageUnresolved && strcmp(nd.image_path, img->path) == 0) return nd.image;
    snprintf(nd.image_path, sizeof(nd.image_path), "%s", img->path);
    char path[512];
    snprintf(path, sizeof(path), "%s%s", s->base_dir, img->path);
    DiagList* diags = new DiagList();
    nd.image = load_image(s->context, path, diags);
    diag_print_text(diags, stderr);
    delete diags;
    return nd.image;
}

uint32_t stack_direction(const Screen* s, int n) {
    if (const ScrollList* sl = widget<ScrollList>(s, n, kWScrollList)) return sl->horizontal ? kDirRow : kDirColumn;
    return layout_of(s, n).direction;
}

bool is_modal(const Screen* s, int n) { return s->nodes[n].widgets[kWModal] != nullptr; }

// Preferred size of n and (recursively) its children, given the space the parent offers.
void measure(Screen* s, int n, float avail_w, float avail_h, uint32_t parent_dir) {
    Node& nd = node(s, n);
    const Layout& l = layout_of(s, n);
    nd.pref_w = nd.pref_h = 0.0f;
    if (!l.visible) return;
    const float sc = s->scale;
    const float pad_x = (l.padding[0] + l.padding[2]) * sc, pad_y = (l.padding[1] + l.padding[3]) * sc;
    float w = -1.0f, h = -1.0f;
    if (l.width > 0.0f) w = l.width * sc;
    else if (l.fill_width && parent_dir != kDirRow) w = avail_w;
    if (l.height > 0.0f) h = l.height * sc;
    else if (l.fill_height && parent_dir != kDirColumn) h = avail_h;
    float inner_w = (w >= 0.0f ? w : avail_w) - pad_x, inner_h = (h >= 0.0f ? h : avail_h) - pad_y;
    if (inner_w < 0.0f) inner_w = 0.0f;
    if (inner_h < 0.0f) inner_h = 0.0f;

    float cw = 0.0f, ch = 0.0f;
    TextSystem* text = context_text(s->context);
    if (const Label* lb = widget<Label>(s, n, kWLabel)) {
        TextStyle st = label_style(s, *lb);
        st.max_width = lb->wrap ? inner_w : 0.0f;
        const TextSize ts = measure_text(text, resolve_text(s->context, lb->text), st);
        cw = ts.width;
        ch = ts.height;
    }
    if (const Button* b = widget<Button>(s, n, kWButton)) {
        const TextSize ts =
            measure_text(text, resolve_text(s->context, b->text), text_style(s, b->font, b->size, b->text_color));
        const float em = b->size * sc;
        if (ts.width + em * 1.5f > cw) cw = ts.width + em * 1.5f;
        if (ts.height + em * 0.6f > ch) ch = ts.height + em * 0.6f;
    }
    if (widget<Image>(s, n, kWImage)) {
        const ImageInfo info = image_info(s->context, node_image(s, n));
        if (info.width && info.height) {
            cw = (float)info.width * sc;
            ch = (float)info.height * sc;
            // One side given: the other keeps the aspect ratio.
            if (w >= 0.0f && h < 0.0f) ch = w * (float)info.height / (float)info.width, cw = w;
            if (h >= 0.0f && w < 0.0f) cw = h * (float)info.width / (float)info.height, ch = h;
        }
    }
    const uint32_t dir = stack_direction(s, n);
    float sum = 0.0f, most_w = 0.0f, most_h = 0.0f;
    uint32_t items = 0;
    for (int c = nd.first_child; c != kNoNode; c = node(s, c).next_sibling) {
        if (is_modal(s, c) || !layout_of(s, c).visible) continue;
        measure(s, c, inner_w, inner_h, dir);
        const Node& cn = node(s, c);
        if (cn.pref_w > most_w) most_w = cn.pref_w;
        if (cn.pref_h > most_h) most_h = cn.pref_h;
        sum += dir == kDirRow ? cn.pref_w : cn.pref_h;
        ++items;
    }
    const float gaps = items > 1 ? l.gap * sc * (float)(items - 1) : 0.0f;
    if (dir == kDirRow) most_w = sum + gaps;
    if (dir == kDirColumn) most_h = sum + gaps;
    if (most_w > cw) cw = most_w;
    if (most_h > ch) ch = most_h;
    nd.pref_w = w >= 0.0f ? w : cw + pad_x;
    nd.pref_h = h >= 0.0f ? h : ch + pad_y;
}

void hide_subtree(Screen* s, int n) {
    Node& nd = node(s, n);
    nd.shown = false;
    nd.rect = nd.clip = Rect{};
    for (int c = nd.first_child; c != kNoNode; c = node(s, c).next_sibling) hide_subtree(s, c);
}

void arrange(Screen* s, int n, const Rect& r, const Rect& clip);

void arrange_modal(Screen* s, int n) {
    measure(s, n, s->width, s->height, kDirNone);
    const Rect screen{0.0f, 0.0f, s->width, s->height};
    arrange(s, n, screen, screen);
}

// Places n at r and lays out its children; appends shown nodes to the draw order.
void arrange(Screen* s, int n, const Rect& r, const Rect& clip) {
    Node& nd = node(s, n);
    const Layout& l = layout_of(s, n);
    if (!l.visible) {
        hide_subtree(s, n);
        return;
    }
    nd.shown = true;
    nd.rect = r;
    nd.clip = clip;
    nd.order = s->order_count;
    s->order[s->order_count++] = n;
    if (is_modal(s, n)) s->top_modal = n;

    const float sc = s->scale;
    const Rect inner{r.x0 + l.padding[0] * sc, r.y0 + l.padding[1] * sc, r.x1 - l.padding[2] * sc,
                     r.y1 - l.padding[3] * sc};
    const float inner_w = inner.x1 > inner.x0 ? inner.x1 - inner.x0 : 0.0f;
    const float inner_h = inner.y1 > inner.y0 ? inner.y1 - inner.y0 : 0.0f;
    ScrollList* list = widget<ScrollList>(s, n, kWScrollList);
    const Rect child_clip = list ? intersect(clip, r) : clip;
    const uint32_t dir = stack_direction(s, n);

    if (dir == kDirNone) {
        for (int c = nd.first_child; c != kNoNode; c = node(s, c).next_sibling) {
            if (is_modal(s, c)) continue;
            const Layout& cl = layout_of(s, c);
            const Node& cn = node(s, c);
            const float w = cl.fill_width && cl.width <= 0.0f ? inner_w : cn.pref_w;
            const float h = cl.fill_height && cl.height <= 0.0f ? inner_h : cn.pref_h;
            const float x = inner.x0 + cl.anchor[0] * (inner_w - w) + cl.offset[0] * sc;
            const float y = inner.y0 + cl.anchor[1] * (inner_h - h) + cl.offset[1] * sc;
            arrange(s, c, Rect{x, y, x + w, y + h}, child_clip);
        }
    } else {
        const bool row = dir == kDirRow;
        const float inner_main = row ? inner_w : inner_h, inner_cross = row ? inner_h : inner_w;
        float total = 0.0f;
        uint32_t items = 0, fills = 0;
        for (int c = nd.first_child; c != kNoNode; c = node(s, c).next_sibling) {
            const Layout& cl = layout_of(s, c);
            if (is_modal(s, c) || !cl.visible) continue;
            total += row ? node(s, c).pref_w : node(s, c).pref_h;
            const bool fill = row ? cl.fill_width && cl.width <= 0.0f : cl.fill_height && cl.height <= 0.0f;
            fills += fill ? 1u : 0u;
            ++items;
        }
        float gap = l.gap * sc;
        total += items > 1 ? gap * (float)(items - 1) : 0.0f;
        const float free_space = inner_main - total;
        float pos = row ? inner.x0 : inner.y0, extra = 0.0f;
        if (list) {
            // Scroll lists stack from the start and move their content by the clamped offset.
            nd.content_main = total;
            const float max_scroll = total > inner_main ? (total - inner_main) / sc : 0.0f;
            if (list->scroll > max_scroll) list->scroll = max_scroll;
            if (list->scroll < 0.0f) list->scroll = 0.0f;
            pos -= list->scroll * sc;
        } else if (fills && free_space > 0.0f) {
            extra = free_space / (float)fills;
        } else if (free_space > 0.0f) {
            if (l.justify == kJustifyCenter) pos += free_space * 0.5f;
            if (l.justify == kJustifyEnd) pos += free_space;
            if (l.justify == kJustifySpaceBetween && items > 1) gap += free_space / (float)(items - 1);
        }
        const float align = l.align == kAlignCenter ? 0.5f : (l.align == kAlignEnd ? 1.0f : 0.0f);
        for (int c = nd.first_child; c != kNoNode; c = node(s, c).next_sibling) {
            if (is_modal(s, c)) continue;
            const Layout& cl = layout_of(s, c);
            if (!cl.visible) {
                hide_subtree(s, c);
                continue;
            }
            const Node& cn = node(s, c);
            const bool fill_main = row ? cl.fill_width && cl.width <= 0.0f : cl.fill_height && cl.height <= 0.0f;
            const bool fill_cross = row ? cl.fill_height && cl.height <= 0.0f : cl.fill_width && cl.width <= 0.0f;
            const float main = (row ? cn.pref_w : cn.pref_h) + (fill_main ? extra : 0.0f);
            const float cross = fill_cross ? inner_cross : (row ? cn.pref_h : cn.pref_w);
            const float cross_pos = (row ? inner.y0 : inner.x0) + align * (inner_cross - cross);
            const float ox = cl.offset[0] * sc, oy = cl.offset[1] * sc;
            const Rect cr = row ? Rect{pos + ox, cross_pos + oy, pos + ox + main, cross_pos + oy + cross}
                                : Rect{cross_pos + ox, pos + oy, cross_pos + ox + cross, pos + oy + main};
            arrange(s, c, cr, child_clip);
            pos += main + gap;
        }
    }
    // Modals cover the whole screen, so they come after their siblings, in drawing and in input.
    for (int c = nd.first_child; c != kNoNode; c = node(s, c).next_sibling) {
        if (is_modal(s, c)) arrange_modal(s, c);
    }
    nd.order_end = s->order_count;
}

void layout_screen(Screen* s) {
    s->order_count = 0;
    s->top_modal = kNoNode;
    const Rect screen{0.0f, 0.0f, s->width, s->height};
    for (int n = s->first_root; n != kNoNode; n = node(s, n).next_sibling) {
        if (is_modal(s, n)) continue;
        measure(s, n, s->width, s->height, kDirNone);
        const Layout& l = layout_of(s, n);
        const Node& nd = node(s, n);
        const float w = l.fill_width && l.width <= 0.0f ? s->width : nd.pref_w;
        const float h = l.fill_height && l.height <= 0.0f ? s->height : nd.pref_h;
        const float x = l.anchor[0] * (s->width - w) + l.offset[0] * s->scale;
        const float y = l.anchor[1] * (s->height - h) + l.offset[1] * s->scale;
        arrange(s, n, Rect{x, y, x + w, y + h}, screen);
    }
    for (int n = s->first_root; n != kNoNode; n = node(s, n).next_sibling) {
        if (is_modal(s, n)) arrange_modal(s, n);
    }
    s->laid_out = true;
}

// Nodes drawn before the top modal (and not inside it) take no input.
bool reachable(const Screen* s, int n) {
    if (n == kNoNode || !node(s, n).shown) return false;
    return s->top_modal == kNoNode || node(s, n).order >= node(s, s->top_modal).order;
}

bool clickable(const Screen* s, int n) {
    const Button* b = widget<Button>(s, n, kWButton);
    return b && b->enabled && reachable(s, n);
}

bool under_pointer(const Screen* s, int n, float x, float y) {
    const Node& nd = node(s, n);
    return contains(nd.rect, x, y) && contains(nd.clip, x, y);
}

// Topmost enabled button under the point, stopping at the top modal.
int hit_button(const Screen* s, float x, float y) {
    for (uint32_t i = s->order_count; i-- > 0;) {
        const int n = s->order[i];
        if (!reachable(s, n)) break;
        if (!under_pointer(s, n, x, y)) continue;
        if (clickable(s, n)) return n;
        if (is_modal(s, n)) break;
    }
    return kNoNode;
}

int hit_list(const Screen* s, float x, float y) {
    for (uint32_t i = s->order_count; i-- > 0;) {
        const int n = s->order[i];
        if (!reachable(s, n)) break;
        if (widget<ScrollList>(s, n, kWScrollList) && under_pointer(s, n, x, y)) return n;
        if (is_modal(s, n) && under_pointer(s, n, x, y)) break;
    }
    return kNoNode;
}

bool hit_surface(const Screen* s, float x, float y) {
    if (s->top_modal != kNoNode) return true;
    for (uint32_t i = 0; i < s->order_count; ++i) {
        const Node& nd = node(s, s->order[i]);
        const bool surface = nd.widgets[kWPanel] || nd.widgets[kWButton] || nd.widgets[kWImage] ||
                             nd.widgets[kWScrollList];
        if (surface && under_pointer(s, s->order[i], x, y)) return true;
    }
    return false;
}

void push_event(Screen* s, EventKind kind, int n) {
    if (s->event_count == kMaxEvents) return;
    Event& e = s->events[s->event_count++];
    e.kind = kind;
    e.node = n;
    const Button* b = n != kNoNode ? widget<Button>(s, n, kWButton) : nullptr;
    snprintf(e.action, sizeof(e.action), "%s", b ? b->action : "");
}

void scroll_by(Screen* s, int list, float pixels) {
    if (ScrollList* sl = widget<ScrollList>(s, list, kWScrollList)) sl->scroll += pixels / s->scale;
}

float axis(const Screen* s, int list, float x, float y) {
    const ScrollList* sl = widget<ScrollList>(s, list, kWScrollList);
    return sl && sl->horizontal ? x : y;
}

// Focus can move to clickable buttons inside the top modal, or anywhere on a navigable screen.
bool in_focus_scope(const Screen* s, int n) {
    if (!clickable(s, n)) return false;
    if (s->top_modal != kNoNode) {
        const Node& m = node(s, s->top_modal);
        return node(s, n).order > m.order && node(s, n).order < m.order_end;
    }
    return s->navigable;
}

int first_focusable(const Screen* s) {
    for (uint32_t i = 0; i < s->order_count; ++i) {
        if (in_focus_scope(s, s->order[i])) return s->order[i];
    }
    return kNoNode;
}

// Nearest focusable node in a direction (dx, dy one of +-1, 0), weighting sideways distance double.
int focus_toward(const Screen* s, int from, float dx, float dy) {
    const Rect& r = node(s, from).rect;
    const float cx = (r.x0 + r.x1) * 0.5f, cy = (r.y0 + r.y1) * 0.5f;
    int best = kNoNode;
    float best_score = 0.0f;
    for (uint32_t i = 0; i < s->order_count; ++i) {
        const int n = s->order[i];
        if (n == from || !in_focus_scope(s, n)) continue;
        const Rect& o = node(s, n).rect;
        const float ox = (o.x0 + o.x1) * 0.5f - cx, oy = (o.y0 + o.y1) * 0.5f - cy;
        const float along = ox * dx + oy * dy;
        if (along <= 0.5f) continue;
        const float side = fabsf(ox * dy) + fabsf(oy * dx);
        const float score = along + 2.0f * side;
        if (best == kNoNode || score < best_score) {
            best = n;
            best_score = score;
        }
    }
    return best;
}

// Scrolls every list around the node so the node is inside it.
void reveal(Screen* s, int n) {
    const Rect target = node(s, n).rect;
    for (int p = node(s, n).parent; p != kNoNode; p = node(s, p).parent) {
        const ScrollList* sl = widget<ScrollList>(s, p, kWScrollList);
        if (!sl) continue;
        const Rect& lr = node(s, p).rect;
        const float t0 = sl->horizontal ? target.x0 : target.y0, t1 = sl->horizontal ? target.x1 : target.y1;
        const float l0 = sl->horizontal ? lr.x0 : lr.y0, l1 = sl->horizontal ? lr.x1 : lr.y1;
        if (t0 < l0) scroll_by(s, p, t0 - l0);
        else if (t1 > l1) scroll_by(s, p, t1 - l1);
    }
}

void apply_pointer(Screen* s, const UiInput& in, float dt) {
    const float x = in.pointer_x, y = in.pointer_y;
    const int button = hit_button(s, x, y);
    s->hover = in.pointer_touch && !in.pointer_down ? kNoNode : button;
    if (in.pointer_pressed) {
        s->focus_visible = false;
        s->pressed = button;
        s->drag_list = hit_list(s, x, y);
        s->dragging = false;
        s->drag_start[0] = s->last_pointer[0] = x;
        s->drag_start[1] = s->last_pointer[1] = y;
        s->velocity = 0.0f;
        s->fling_list = kNoNode;
        s->pointer_down = true;
        s->captured = hit_surface(s, x, y);
    }
    if (s->pointer_down && in.pointer_down && s->drag_list != kNoNode) {
        const float moved = axis(s, s->drag_list, x - s->drag_start[0], y - s->drag_start[1]);
        if (!s->dragging && fabsf(moved) > kDragThreshold * s->scale) {
            s->dragging = true;
            s->pressed = kNoNode;
        }
        if (s->dragging) {
            const float delta = axis(s, s->drag_list, x - s->last_pointer[0], y - s->last_pointer[1]);
            scroll_by(s, s->drag_list, -delta);
            if (dt > 0.0f) s->velocity = s->velocity * 0.5f + (delta / dt) * 0.5f;
        }
    }
    s->last_pointer[0] = x;
    s->last_pointer[1] = y;
    if (in.pointer_released && s->pointer_down) {
        if (s->pressed != kNoNode && s->pressed == button) push_event(s, EventKind::kClick, button);
        if (s->dragging) s->fling_list = s->drag_list;
        s->pressed = kNoNode;
        s->drag_list = kNoNode;
        s->dragging = false;
        s->pointer_down = false;
    }
    if (!in.pointer_down && !in.pointer_released) {
        s->pressed = kNoNode;
        s->pointer_down = false;
    }
    if (!s->pointer_down) s->captured = hit_surface(s, x, y);
    if (in.wheel_y != 0.0f) {
        const int list = hit_list(s, x, y);
        if (list != kNoNode) scroll_by(s, list, -in.wheel_y * kWheelStep * s->scale);
    }
    if (s->fling_list != kNoNode) {
        scroll_by(s, s->fling_list, -s->velocity * dt);
        s->velocity *= expf(-kFlingDecay * dt);
        if (fabsf(s->velocity) < 20.0f * s->scale) s->fling_list = kNoNode;
    }
}

void apply_navigation(Screen* s, const UiInput& in) {
    if (s->focus != kNoNode && !in_focus_scope(s, s->focus)) s->focus = kNoNode;
    const float dx = in.nav_left ? -1.0f : (in.nav_right ? 1.0f : 0.0f);
    const float dy = in.nav_up ? -1.0f : (in.nav_down ? 1.0f : 0.0f);
    if (dx != 0.0f || dy != 0.0f) {
        if (s->focus == kNoNode || !s->focus_visible) {
            if (s->focus == kNoNode) s->focus = first_focusable(s);
        } else {
            const int next = focus_toward(s, s->focus, dx, dy);
            if (next != kNoNode) s->focus = next;
        }
        if (s->focus != kNoNode) {
            s->focus_visible = true;
            reveal(s, s->focus);
        }
    }
    if (in.nav_confirm && s->focus != kNoNode && s->focus_visible) push_event(s, EventKind::kClick, s->focus);
    if (in.nav_back) push_event(s, EventKind::kBack, kNoNode);
}

void draw_image(Screen* s, DrawList* list, int n) {
    const Image* img = widget<Image>(s, n, kWImage);
    const ImageInfo info = image_info(s->context, node_image(s, n));
    if (!info.width || !info.height) return;
    Rect r = node(s, n).rect;
    if (img->fit == kFitContain) {
        const float rw = r.x1 - r.x0, rh = r.y1 - r.y0;
        const float k = fminf(rw / (float)info.width, rh / (float)info.height);
        const float w = (float)info.width * k, h = (float)info.height * k;
        r.x0 += (rw - w) * 0.5f;
        r.y0 += (rh - h) * 0.5f;
        r.x1 = r.x0 + w;
        r.y1 = r.y0 + h;
    }
    Quad* q = push_quad(list);
    q->rect[0] = r.x0;
    q->rect[1] = r.y0;
    q->rect[2] = r.x1;
    q->rect[3] = r.y1;
    memcpy(q->uv, info.uv, sizeof(q->uv));
    q->color = img->tint;
    q->kind = kQuadImage;
}

void draw_node(Screen* s, DrawList* list, int n) {
    const Node& nd = node(s, n);
    if (!nd.shown) return;
    const float sc = s->scale;
    const Rect& r = nd.rect;
    TextSystem* text = context_text(s->context);
    if (const Modal* m = widget<Modal>(s, n, kWModal)) draw_rect(list, 0.0f, 0.0f, s->width, s->height, m->dim_color);
    if (const Panel* p = widget<Panel>(s, n, kWPanel)) {
        draw_rect(list, r.x0, r.y0, r.x1, r.y1, p->color, p->radius * sc, p->border * sc, p->border_color);
    }
    if (widget<Image>(s, n, kWImage)) draw_image(s, list, n);
    if (const Button* b = widget<Button>(s, n, kWButton)) {
        uint32_t color = b->color;
        if (!b->enabled) color = b->disabled_color;
        else if (s->pressed == n && s->hover == n) color = b->pressed_color;
        else if (s->hover == n) color = b->hover_color;
        draw_rect(list, r.x0, r.y0, r.x1, r.y1, color, b->radius * sc);
        const TextStyle st = text_style(s, b->font, b->size, b->text_color);
        const char* caption = resolve_text(s->context, b->text);
        const TextSize ts = measure_text(text, caption, st);
        draw_text(text, list, floorf((r.x0 + r.x1 - ts.width) * 0.5f + 0.5f), (r.y0 + r.y1 - ts.height) * 0.5f, caption,
                  st);
        if (s->focus == n && s->focus_visible) {
            const float g = kFocusRingGap * sc;
            draw_rect(list, r.x0 - g, r.y0 - g, r.x1 + g, r.y1 + g, 0, b->radius * sc + g, kFocusRingWidth * sc,
                      kFocusRingColor);
        }
    }
    if (const Label* lb = widget<Label>(s, n, kWLabel)) {
        TextStyle st = label_style(s, *lb);
        const char* value = resolve_text(s->context, lb->text);
        const float w = r.x1 - r.x0;
        float x = r.x0;
        if (lb->wrap) {
            st.max_width = w;
        } else if (lb->align != kTextLeft) {
            const float tw = measure_text(text, value, st).width;
            x += (w - tw) * (lb->align == kTextCenter ? 0.5f : 1.0f);
            st.align = Align::kLeft;
        }
        const float th = measure_text(text, value, st).height;
        draw_text(text, list, x, r.y0 + (r.y1 - r.y0 - th) * 0.5f, value, st);
    }
    float saved[4];
    memcpy(saved, list->clip, sizeof(saved));
    if (widget<ScrollList>(s, n, kWScrollList)) {
        const Rect c = intersect(Rect{saved[0], saved[1], saved[2], saved[3]}, r);
        list->clip[0] = c.x0;
        list->clip[1] = c.y0;
        list->clip[2] = c.x1;
        list->clip[3] = c.y1;
    }
    for (int c = nd.first_child; c != kNoNode; c = node(s, c).next_sibling) {
        if (!is_modal(s, c)) draw_node(s, list, c);
    }
    memcpy(list->clip, saved, sizeof(saved));
    for (int c = nd.first_child; c != kNoNode; c = node(s, c).next_sibling) {
        if (is_modal(s, c)) draw_node(s, list, c);
    }
}

}  // namespace

Screen* create_screen(Context* context, const char* name) {
    Screen* s = new Screen();
    s->context = context;
    snprintf(s->name, sizeof(s->name), "%s", name);
    return s;
}

void destroy_screen(Screen* s) {
    if (!s) return;
    for (uint32_t i = 0; i < s->count; ++i) {
        for (uint32_t t = 0; t < kWidgetTypes; ++t) {
            if (s->nodes[i].owned & (1u << t)) free(s->nodes[i].widgets[t]);
        }
    }
    free(s->nodes);
    free(s->order);
    delete s;
}

const char* screen_name(const Screen* s) { return s->name; }
Context* screen_context(Screen* s) { return s->context; }

int add_node(Screen* s, const char* name, int parent) {
    if (s->count == s->capacity) {
        s->capacity = s->capacity ? s->capacity * 2 : 64;
        s->nodes = (Node*)realloc(s->nodes, sizeof(Node) * s->capacity);
        s->order = (int*)realloc(s->order, sizeof(int) * s->capacity);
    }
    const int n = (int)s->count++;
    Node& nd = s->nodes[n];
    nd = Node{};
    snprintf(nd.name, sizeof(nd.name), "%s", name);
    nd.parent = parent;
    nd.first_child = nd.last_child = nd.next_sibling = kNoNode;
    nd.image = kImageUnresolved;
    int* first = parent == kNoNode ? &s->first_root : &s->nodes[parent].first_child;
    int* last = parent == kNoNode ? &s->last_root : &s->nodes[parent].last_child;
    if (*last != kNoNode) s->nodes[*last].next_sibling = n;
    else *first = n;
    *last = n;
    return n;
}

uint32_t node_count(const Screen* s) { return s->count; }

int find_node(const Screen* s, const char* name) {
    for (uint32_t i = 0; i < s->count; ++i) {
        if (strcmp(s->nodes[i].name, name) == 0) return (int)i;
    }
    return kNoNode;
}

const char* node_name(const Screen* s, int n) { return n >= 0 && (uint32_t)n < s->count ? s->nodes[n].name : ""; }
int node_parent(const Screen* s, int n) { return s->nodes[n].parent; }

void* add_widget(Screen* s, int n, const reflect::ComponentDesc* desc) {
    const uint32_t t = widget_type(desc);
    if (t == kWidgetTypes) return nullptr;
    Node& nd = s->nodes[n];
    if (nd.widgets[t]) return nd.widgets[t];
    void* data = malloc(desc->size);
    memcpy(data, desc->defaults, desc->size);
    nd.widgets[t] = data;
    nd.owned |= 1u << t;
    return data;
}

void attach_widget(Screen* s, int n, const reflect::ComponentDesc* desc, void* data) {
    const uint32_t t = widget_type(desc);
    if (t == kWidgetTypes) return;
    Node& nd = s->nodes[n];
    if (nd.owned & (1u << t)) free(nd.widgets[t]);
    nd.owned &= ~(1u << t);
    nd.widgets[t] = data;
}

void* get_widget(const Screen* s, int n, const reflect::ComponentDesc* desc) {
    const uint32_t t = widget_type(desc);
    return t == kWidgetTypes ? nullptr : s->nodes[n].widgets[t];
}

void set_base_dir(Screen* s, const char* dir) { snprintf(s->base_dir, sizeof(s->base_dir), "%s", dir); }
void set_navigable(Screen* s, bool navigable) { s->navigable = navigable; }

void set_reference(Screen* s, float width, float height) {
    s->reference[0] = width > 0.0f ? width : 1280.0f;
    s->reference[1] = height > 0.0f ? height : 720.0f;
}

float screen_scale(const Screen* s, float width, float height) {
    const float x = width / s->reference[0], y = height / s->reference[1];
    return x < y ? x : y;
}

Screen* build_screen(Context* context, scene::Scene* doc) {
    Screen* s = create_screen(context, doc->name);
    set_navigable(s, doc->navigable);
    set_reference(s, doc->reference[0], doc->reference[1]);
    // Image paths are relative to the screen file.
    char dir[260];
    snprintf(dir, sizeof(dir), "%s", doc->path);
    char* slash = strrchr(dir, '/');
    char* back = strrchr(dir, '\\');
    if (back && (!slash || back > slash)) slash = back;
    if (slash) slash[1] = 0;
    else dir[0] = 0;
    set_base_dir(s, dir);
    for (uint32_t i = 0; i < doc->entity_count; ++i) {
        scene::Entity& e = doc->entities[i];
        const int n = add_node(s, e.name, e.parent_index);
        for (uint32_t c = 0; c < e.component_count; ++c) attach_widget(s, n, e.components[c].desc, e.components[c].data);
    }
    return s;
}

void update_screen(Screen* s, const UiInput& input, float width, float height, float scale, float dt) {
    s->event_count = 0;
    if (s->laid_out) {
        apply_pointer(s, input, dt);
        apply_navigation(s, input);
    }
    s->width = width;
    s->height = height;
    s->scale = scale > 0.0f ? scale : 1.0f;
    layout_screen(s);
}

uint32_t event_count(const Screen* s) { return s->event_count; }
const Event* event_at(const Screen* s, uint32_t i) { return i < s->event_count ? &s->events[i] : nullptr; }
bool pointer_captured(const Screen* s) { return s->captured; }

void draw_screen(Screen* s, DrawList* list) {
    for (int n = s->first_root; n != kNoNode; n = node(s, n).next_sibling) {
        if (!is_modal(s, n)) draw_node(s, list, n);
    }
    for (int n = s->first_root; n != kNoNode; n = node(s, n).next_sibling) {
        if (is_modal(s, n)) draw_node(s, list, n);
    }
}

Rect node_rect(const Screen* s, int n) { return s->nodes[n].rect; }
bool node_shown(const Screen* s, int n) { return s->nodes[n].shown; }
int focused_node(const Screen* s) { return s->focus; }
bool focus_visible(const Screen* s) { return s->focus_visible; }
int hovered_node(const Screen* s) { return s->hover; }
int pressed_node(const Screen* s) { return s->pressed; }

void set_focus(Screen* s, int n, bool visible) {
    s->focus = n;
    s->focus_visible = n != kNoNode && visible;
}

}  // namespace zaro::ui
