// UI widgets: plain reflected structs attached to screen nodes (D-020), like scene components.
// Sizes and offsets are in design units; the screen's scale turns them into pixels.
#pragma once

#include "core/reflect.h"

#include <stdint.h>

namespace zaro::ui {

enum Direction : uint32_t { kDirNone, kDirRow, kDirColumn };
enum AlignItems : uint32_t { kAlignStart, kAlignCenter, kAlignEnd };
enum Justify : uint32_t { kJustifyStart, kJustifyCenter, kJustifyEnd, kJustifySpaceBetween };
enum TextAlign : uint32_t { kTextLeft, kTextCenter, kTextRight };
enum ImageFit : uint32_t { kFitStretch, kFitContain };

// Where a node goes and how it places its children. A node without one uses these defaults.
struct Layout {
    float width = 0.0f;   // 0 fits the content
    float height = 0.0f;
    bool fill_width = false;   // take the parent's inner width (in a row: share the free width)
    bool fill_height = false;  // take the parent's inner height (in a column: share the free height)
    float anchor[2] = {0.0f, 0.0f};  // in a free parent: 0 left/top, 0.5 centre, 1 right/bottom
    float offset[2] = {0.0f, 0.0f};  // moves the node after placement
    uint32_t direction = kDirNone;   // children: free (anchored), row or column
    float padding[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // left, top, right, bottom
    float gap = 0.0f;                // between children of a row or column
    uint32_t align = kAlignStart;    // children across a row or column
    uint32_t justify = kJustifyStart;  // children along a row or column
    bool visible = true;
};

// A filled rectangle behind the node's children.
struct Panel {
    uint32_t color = 0xff302820u;  // #202830
    float radius = 0.0f;
    float border = 0.0f;
    uint32_t border_color = 0xffffffffu;
};

struct Label {
    char text[reflect::kStringCapacity] = {};
    char font[reflect::kStringCapacity] = "regular";
    float size = 24.0f;
    uint32_t color = 0xffffffffu;
    uint32_t align = kTextLeft;
    bool wrap = false;  // break lines at the node's width
    float line_spacing = 1.0f;
};

struct Image {
    char path[reflect::kStringCapacity] = {};
    uint32_t tint = 0xffffffffu;
    uint32_t fit = kFitContain;
};

// A pressable rectangle with a centred caption. A click emits the action.
struct Button {
    char text[reflect::kStringCapacity] = {};
    char action[reflect::kStringCapacity] = {};
    char font[reflect::kStringCapacity] = "bold";
    float size = 24.0f;
    uint32_t text_color = 0xffffffffu;
    uint32_t color = 0xffb06a3au;          // #3a6ab0
    uint32_t hover_color = 0xffc87d47u;    // #477dc8
    uint32_t pressed_color = 0xff8a5230u;  // #30528a
    uint32_t disabled_color = 0xff504a48u; // #484a50
    float radius = 8.0f;
    bool enabled = true;
    char sound[reflect::kStringCapacity] = {};  // a sound of the scene's sound file, played on a click (D-032)
};

// Scrolls its children, which it stacks like a column (or a row when horizontal), clipped to its
// rectangle. Drag, fling and the mouse wheel scroll it.
struct ScrollList {
    bool horizontal = false;
    float scroll = 0.0f;  // offset in design units from the start, clamped to the content
};

// Covers the whole screen, dims what is behind it and takes every pointer and navigation input
// until it is hidden. Its children are laid out in the full screen.
struct Modal {
    uint32_t dim_color = 0x99000000u;  // #00000099
};

extern const reflect::ComponentDesc kLayoutDesc;
extern const reflect::ComponentDesc kPanelDesc;
extern const reflect::ComponentDesc kLabelDesc;
extern const reflect::ComponentDesc kImageDesc;
extern const reflect::ComponentDesc kButtonDesc;
extern const reflect::ComponentDesc kScrollListDesc;
extern const reflect::ComponentDesc kModalDesc;

enum WidgetType : uint32_t { kWLayout, kWPanel, kWLabel, kWImage, kWButton, kWScrollList, kWModal, kWidgetTypes };
// The descriptor of each WidgetType, in order.
const reflect::ComponentDesc* widget_desc(uint32_t type);
// WidgetType of a descriptor, or kWidgetTypes if it is not a widget.
uint32_t widget_type(const reflect::ComponentDesc* desc);

void register_ui_components();

}  // namespace zaro::ui
