// A screen: a tree of named nodes carrying widgets (D-020). Each frame update_screen() applies the
// input (against the previous frame's layout), then lays the tree out; draw_screen() appends its
// quads to a DrawList.
//
// Layout: every node has a rectangle. A free parent (direction "none") places each child by its
// anchor and offset; a row or column stacks them with gap, align and justify, and children with
// fill share the free space. Sizes of 0 fit the content (text, image, children).
//
// Input: pointer presses and releases on a button make a click; dragging a scroll list scrolls
// it (with fling) and cancels the press. The topmost visible modal takes all input. The arrow
// keys or d-pad move the focus between enabled buttons inside the top modal, or on the whole
// screen when it is navigable; confirm clicks the focused button. The focus ring shows only after
// navigation input and hides again on the next pointer press.
#pragma once

#include "core/reflect.h"
#include "scene/scene.h"
#include "ui/context.h"
#include "ui/draw.h"
#include "ui/widgets.h"

#include <stdint.h>

namespace zaro::ui {

struct Screen;

constexpr int kNoNode = -1;

struct Rect {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};

// Input for one frame, in target pixels. The app fills it from platform::Input.
struct UiInput {
    float pointer_x = -1.0f, pointer_y = -1.0f;
    bool pointer_down = false;
    bool pointer_pressed = false;
    bool pointer_released = false;
    bool pointer_touch = false;
    float wheel_y = 0.0f;
    bool nav_up = false, nav_down = false, nav_left = false, nav_right = false;
    bool nav_confirm = false;
    bool nav_back = false;
};

enum class EventKind : uint8_t {
    kClick,  // a button was clicked or confirmed; `action` is its action
    kBack,   // back was pressed (the navigation policy decides what closes)
};

struct Event {
    EventKind kind;
    int node;  // the button, or kNoNode
    char action[reflect::kStringCapacity];
};

Screen* create_screen(Context* context, const char* name);
// Builds a screen from a loaded screen file. The screen uses the document's widget data in place
// (so scripts that change it change the screen); the document must outlive the screen. Image paths
// resolve against the file's directory.
Screen* build_screen(Context* context, scene::Scene* document);
void destroy_screen(Screen* screen);
const char* screen_name(const Screen* screen);
Context* screen_context(Screen* screen);

// Adds a node under `parent` (kNoNode for the top level). Siblings keep the order they were added
// in, which is also the drawing order: later siblings draw on top. Returns the node index.
int add_node(Screen* screen, const char* name, int parent);
uint32_t node_count(const Screen* screen);
int find_node(const Screen* screen, const char* name);
const char* node_name(const Screen* screen, int node);
int node_parent(const Screen* screen, int node);
// Adds a widget with its default values, owned by the screen, and returns it to fill in. If the
// node already has a widget of that type, returns that one.
void* add_widget(Screen* screen, int node, const reflect::ComponentDesc* desc);
// Uses widget data owned elsewhere (a loaded screen file); it must outlive the screen.
void attach_widget(Screen* screen, int node, const reflect::ComponentDesc* desc, void* data);
// The node's widget of that type, or nullptr.
void* get_widget(const Screen* screen, int node, const reflect::ComponentDesc* desc);

// Image paths are relative to this directory (with a trailing separator; "" by default).
void set_base_dir(Screen* screen, const char* dir);
// Lets navigation input move the focus on the whole screen, not only inside an open modal.
void set_navigable(Screen* screen, bool navigable);
// Design size the screen was made for (1280 x 720 by default). screen_scale is the largest scale
// at which it fits a width x height target.
void set_reference(Screen* screen, float width, float height);
float screen_scale(const Screen* screen, float width, float height);

// Applies the input, then lays the screen out for a width x height pixel target where one design
// unit is `scale` pixels. dt is the frame time in seconds (for fling). Events stay until the next
// update.
void update_screen(Screen* screen, const UiInput& input, float width, float height, float scale, float dt);
uint32_t event_count(const Screen* screen);
const Event* event_at(const Screen* screen, uint32_t index);
// True while the pointer is over a panel, button, image or list of the screen, a modal is open,
// or a press or drag that started on the screen continues; the game should then ignore it.
bool pointer_captured(const Screen* screen);

// Appends the screen's quads. Call after update_screen.
void draw_screen(Screen* screen, DrawList* list);

Rect node_rect(const Screen* screen, int node);
bool node_shown(const Screen* screen, int node);  // visible itself and all its ancestors
int focused_node(const Screen* screen);
bool focus_visible(const Screen* screen);
void set_focus(Screen* screen, int node, bool visible);
int hovered_node(const Screen* screen);
int pressed_node(const Screen* screen);

}  // namespace zaro::ui
