// The scene's UI: its root screens and the screens it can open (ui::Navigator, D-021), their string
// tables and fonts, the view model scripts fill, and the hooks scripts receive. Updated from platform
// input and drawn over the frame; wires ui (model) to render (view) like the crowd.
#pragma once

#include "core/diag.h"
#include "platform/platform.h"
#include "rhi/rhi.h"
#include "script/script.h"

#include <stddef.h>
#include <stdint.h>

namespace zaro::app {

struct UiLayer;

// Loads the scene's root screens ([scene] ui), registers the screens it can open ([scene] screens)
// and the string table of `language` (nullptr: the scene's first), with the bundled fonts. Returns
// nullptr when the scene has no ui, or after adding diagnostics (*failed is then true).
UiLayer* create_ui_layer(scene::Scene* scene, const char* language, DiagList* diags, bool* failed);
void destroy_ui_layer(UiLayer* layer);

// Creates the GPU side; false if the UI shader is missing.
bool attach_gpu(UiLayer* layer, rhi::Device* device, const char* shader_dir);

// What the script's `ui` table calls, and what it writes to (pass both in script::ScriptDesc).
const script::UiHost* ui_host(UiLayer* layer);
scene::UiState* ui_state(UiLayer* layer);
// The script that receives on_ui, on_back and on_screen; nullptr while there is none (or it is paused).
void set_script(UiLayer* layer, script::Script* script);

// Applies the input (nullptr: none, as in headless runs), advances transitions and lays every
// screen out for the target. Clicks and navigation are handled by handle_ui.
void update_ui(UiLayer* layer, const platform::Input* input, float width, float height, float dt);
// Handles this frame's clicks (open:<screen>, close, back and home actions, then on_ui for every
// action), a back press (on_back may claim it, then the navigation policy), and sends navigation
// events to on_screen. Diagnostics (a failing hook, an unknown screen) go to diags; returns false
// if a script hook failed.
bool handle_ui(UiLayer* layer, bool back_pressed, DiagList* diags);
// True if a back press this frame found nothing to close and nothing took it: the game decides (the
// crowd demo quits).
bool back_unhandled(const UiLayer* layer);
// True if this frame's pointer went to the UI rather than the game.
bool ui_captured_input(const UiLayer* layer);

// Records the "ui" pass. Call between rhi::begin_frame and rhi::end_frame, after the scene's passes.
void render_ui_layer(UiLayer* layer);

// Switches to another of the scene's languages. False with a message in err otherwise.
bool set_language(UiLayer* layer, const char* language, char* err, size_t err_size);
// Clicks a button by screen and node name, as a press and release would (scene tests, --click).
// Call after update_ui and before handle_ui. False if no shown, enabled, unblocked button matches.
bool click(UiLayer* layer, const char* screen, const char* node);
// The topmost shown instance of a screen's file, or nullptr.
scene::Scene* find_document(UiLayer* layer, const char* screen);
// The text a node shows, or nullptr if the screen is not shown or has no such node.
const char* node_text(UiLayer* layer, const char* screen, const char* node, char* out, size_t size);
// "opening", "entering", "open", "closing" or "closed" (roots are always "open").
const char* screen_state(UiLayer* layer, const char* screen);

// `zaro check`: loads the scene's screens (roots and openable) and every language's string table
// and reports missing keys, tables that disagree, missing images and open:<screen> actions that
// name no screen of the scene. Returns false if anything was reported.
bool check_scene_ui(const scene::Scene* scene, DiagList* diags);
// `zaro check` on a screen file alone: its images (string keys need a scene's tables).
bool check_screen_images(const scene::Scene* screen, DiagList* diags);

}  // namespace zaro::app
