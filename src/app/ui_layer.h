// The scene's UI: the screens listed in [scene] ui, their string tables and fonts, updated from
// platform input and drawn over the frame. Wires ui (model) to render (view) like the crowd.
#pragma once

#include "core/diag.h"
#include "platform/platform.h"
#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro {
namespace scene {
struct Scene;
struct UiState;
}
namespace ui {
struct Screen;
}
}  // namespace zaro

namespace zaro::app {

struct UiLayer;

// Loads the scene's screens and the string table of `language` (nullptr: the scene's first
// language), with the bundled fonts. Returns nullptr when the scene has no ui, or after adding
// diagnostics (*failed is then true).
UiLayer* create_ui_layer(scene::Scene* scene, const char* language, DiagList* diags, bool* failed);
void destroy_ui_layer(UiLayer* layer);

// Creates the GPU side; false if the UI shader is missing.
bool attach_gpu(UiLayer* layer, rhi::Device* device, const char* shader_dir);

// Applies the input (nullptr: none, as in headless runs) and lays every screen out for the target.
// The top screen gets the pointer first; screens below it see none while it captures the pointer.
void update_ui(UiLayer* layer, const platform::Input* input, float width, float height, float dt);
// True if a back press reached the UI this frame and something on screen used it. Until the
// navigation policy exists nothing does, so the app decides (the crowd demo quits).
bool ui_used_back(const UiLayer* layer);

// Records the "ui" pass. Call between rhi::begin_frame and rhi::end_frame, after the scene's passes.
void render_ui_layer(UiLayer* layer);

uint32_t screen_count(const UiLayer* layer);
ui::Screen* screen_at(UiLayer* layer, uint32_t index);
ui::Screen* find_screen(UiLayer* layer, const char* name);
// The loaded screen files, bottom to top (scripts change their widget fields in place).
scene::Scene* const* screen_documents(UiLayer* layer);
// The view-model values the texts show and the commands scripts send (scene/ui_state.h). The layer
// owns it and keeps its value "language" current. nullptr for a null layer.
scene::UiState* ui_state(UiLayer* layer);

// Switches to another of the scene's languages. False (with a ZE0703 diagnostic) if its table
// does not load; the current language stays.
bool set_language(UiLayer* layer, const char* language, DiagList* diags);
// Clicks a button by screen and node name (see ui::click_node). Call after update_ui.
bool click(UiLayer* layer, const char* screen, const char* node);
// The text a node shows, or nullptr if the screen or node does not exist.
const char* node_text(UiLayer* layer, const char* screen, const char* node, char* out, size_t size);

// `zaro check`: loads the scene's screens and every language's string table and reports missing
// keys, tables that disagree and missing images. Returns false if anything was reported.
bool check_scene_ui(const scene::Scene* scene, DiagList* diags);
// `zaro check` on a screen file alone: its images (string keys need a scene's tables).
bool check_screen_images(const scene::Scene* screen, DiagList* diags);

}  // namespace zaro::app
