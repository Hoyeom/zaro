#include "ui/widgets.h"

namespace zaro::ui {

namespace {

const char* const kDirections[] = {"none", "row", "column", nullptr};
const char* const kAligns[] = {"start", "center", "end", nullptr};
const char* const kJustifies[] = {"start", "center", "end", "space_between", nullptr};
const char* const kTextAligns[] = {"left", "center", "right", nullptr};
const char* const kFits[] = {"stretch", "contain", nullptr};

const reflect::FieldDesc kLayoutFields[] = {
    ZARO_FIELD_RANGE(Layout, width, kF32, "Width in design units; 0 fits the content", 0.0, 100000.0),
    ZARO_FIELD_RANGE(Layout, height, kF32, "Height in design units; 0 fits the content", 0.0, 100000.0),
    ZARO_FIELD(Layout, fill_width, kBool, "Take the parent's inner width; in a row, share the free width"),
    ZARO_FIELD(Layout, fill_height, kBool, "Take the parent's inner height; in a column, share the free height"),
    ZARO_FIELD(Layout, anchor, kVec2, "Place in a free parent: 0 left/top, 0.5 centre, 1 right/bottom"),
    ZARO_FIELD(Layout, offset, kVec2, "Move the node after placement, in design units"),
    ZARO_FIELD_ENUM(Layout, direction, "Children: none places each by its anchor; row and column stack them",
                    kDirections),
    ZARO_FIELD(Layout, padding, kVec4, "Inner space: left, top, right, bottom"),
    ZARO_FIELD_RANGE(Layout, gap, kF32, "Space between the children of a row or column", 0.0, 10000.0),
    ZARO_FIELD_ENUM(Layout, align, "Children across a row or column", kAligns),
    ZARO_FIELD_ENUM(Layout, justify, "Children along a row or column", kJustifies),
    ZARO_FIELD(Layout, visible, kBool, "Hidden nodes and their children are not drawn and take no input"),
};

const reflect::FieldDesc kPanelFields[] = {
    ZARO_FIELD(Panel, color, kColor, "Fill colour"),
    ZARO_FIELD_RANGE(Panel, radius, kF32, "Corner radius in design units", 0.0, 10000.0),
    ZARO_FIELD_RANGE(Panel, border, kF32, "Border width in design units, inside the rectangle", 0.0, 1000.0),
    ZARO_FIELD(Panel, border_color, kColor, "Border colour"),
};

const reflect::FieldDesc kLabelFields[] = {
    ZARO_FIELD(Label, text, kString, "UTF-8 text"),
    ZARO_FIELD(Label, font, kString, "Font name: regular or bold"),
    ZARO_FIELD_RANGE(Label, size, kF32, "Font size (em) in design units", 1.0, 512.0),
    ZARO_FIELD(Label, color, kColor, "Text colour"),
    ZARO_FIELD_ENUM(Label, align, "Horizontal alignment in the node", kTextAligns),
    ZARO_FIELD(Label, wrap, kBool, "Break lines at the node's width"),
    ZARO_FIELD_RANGE(Label, line_spacing, kF32, "Line height as a multiple of the font's", 0.5, 4.0),
};

const reflect::FieldDesc kImageFields[] = {
    ZARO_FIELD(Image, path, kString, "PNG file, relative to the screen file"),
    ZARO_FIELD(Image, tint, kColor, "Multiplies the image's colours"),
    ZARO_FIELD_ENUM(Image, fit, "stretch fills the node; contain keeps the aspect ratio inside it", kFits),
};

const reflect::FieldDesc kButtonFields[] = {
    ZARO_FIELD(Button, text, kString, "Caption, centred"),
    ZARO_FIELD(Button, action, kString, "Event name sent when the button is clicked"),
    ZARO_FIELD(Button, font, kString, "Font name: regular or bold"),
    ZARO_FIELD_RANGE(Button, size, kF32, "Caption font size in design units", 1.0, 512.0),
    ZARO_FIELD(Button, text_color, kColor, "Caption colour"),
    ZARO_FIELD(Button, color, kColor, "Fill colour"),
    ZARO_FIELD(Button, hover_color, kColor, "Fill colour while a mouse is over it"),
    ZARO_FIELD(Button, pressed_color, kColor, "Fill colour while pressed"),
    ZARO_FIELD(Button, disabled_color, kColor, "Fill colour when not enabled"),
    ZARO_FIELD_RANGE(Button, radius, kF32, "Corner radius in design units", 0.0, 10000.0),
    ZARO_FIELD(Button, enabled, kBool, "Disabled buttons ignore input and cannot take focus"),
};

const reflect::FieldDesc kScrollListFields[] = {
    ZARO_FIELD(ScrollList, horizontal, kBool, "Stack and scroll the children left to right"),
    ZARO_FIELD_RANGE(ScrollList, scroll, kF32, "Scroll offset in design units", 0.0, 1000000.0),
};

const reflect::FieldDesc kModalFields[] = {
    ZARO_FIELD(Modal, dim_color, kColor, "Drawn over everything behind the modal"),
};

}  // namespace

ZARO_COMPONENT(Layout, "Size, placement in the parent, and how children are arranged", kLayoutFields);
ZARO_COMPONENT(Panel, "Filled rectangle with optional rounded corners and border", kPanelFields);
ZARO_COMPONENT(Label, "Text", kLabelFields);
ZARO_COMPONENT(Image, "PNG image", kImageFields);
ZARO_COMPONENT(Button, "Pressable button with a caption; a click sends its action", kButtonFields);
ZARO_COMPONENT(ScrollList, "Clips and scrolls its children, stacked in a column or row", kScrollListFields);
ZARO_COMPONENT(Modal, "Full-screen layer that dims what is behind it and takes all input", kModalFields);

namespace {

const reflect::ComponentDesc* const kWidgets[kWidgetTypes] = {
    &kLayoutDesc, &kPanelDesc, &kLabelDesc, &kImageDesc, &kButtonDesc, &kScrollListDesc, &kModalDesc,
};

}  // namespace

const reflect::ComponentDesc* widget_desc(uint32_t type) { return type < kWidgetTypes ? kWidgets[type] : nullptr; }

uint32_t widget_type(const reflect::ComponentDesc* desc) {
    for (uint32_t i = 0; i < kWidgetTypes; ++i) {
        if (kWidgets[i] == desc) return i;
    }
    return kWidgetTypes;
}

void register_ui_components() {
    for (const reflect::ComponentDesc* d : kWidgets) reflect::register_component(d);
}

}  // namespace zaro::ui
