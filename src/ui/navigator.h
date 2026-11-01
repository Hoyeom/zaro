// UI navigation policy (D-021): a stack of screens over root screens, with back, close, home and
// abort-all as separate operations, and a lifecycle per opened screen.
//
// Root screens (the HUD) sit at the bottom, are always shown and are never closed by navigation.
// Catalog screens are opened by name; each open is a new instance unless the caller passes a key
// that an open instance already has, which brings that one to the top instead.
//
// An opened screen goes opening -> entering -> open -> closing -> gone, never backwards:
//   opening   only when opened with `wait`: a spinner shows until ready(); no input
//   entering  its entrance transition plays; no input
//   open      takes input (unless a modal above it blocks it)
//   closing   its exit transition plays; no input; input already belongs to the screen below
//
// back() decides from the topmost screen that is not closing:
//   opening   dismissible: cancelled (removed at once); forced: ignored
//   entering  forced: ignored; skippable: the transition jumps to its end, so the next back is a
//             real back; otherwise the press is dropped (never queued)
//   open      dismissible: closes; forced: the press is consumed and the screen stays
//   none      nothing closes and kBackAtRoot is reported, for the game to decide
// A claim (the game taking the press for something inside the screen) comes before all of this.
//
// close() plays the exit transition; abort_all() removes every opened screen at once, forced ones
// included (scene change, logout). home() closes down to the root and stops at a forced screen.
#pragma once

#include "core/diag.h"
#include "scene/scene.h"
#include "ui/context.h"
#include "ui/draw.h"
#include "ui/screen.h"

#include <stdint.h>

namespace zaro::ui {

struct Navigator;

enum class EntryState : uint8_t { kOpening, kEntering, kOpen, kClosing };

enum class BackResult : uint8_t {
    kClaimed,    // the game took the press
    kCancelled,  // an opening screen was cancelled
    kSnapped,    // an entrance transition jumped to its end
    kDropped,    // the press came during an unskippable entrance, or a forced screen was opening
    kClosing,    // the top screen started closing
    kConsumed,   // a forced screen kept the press
    kBackAtRoot, // nothing to close
};

enum class NavEventKind : uint8_t {
    kCreated,     // a new instance was pushed, before its entrance: fill it in now
    kOpened,      // finished its entrance and takes input
    kClosed,      // gone (after its exit transition, or aborted)
    kCancelled,   // closed by back while it was still opening
    kBackAtRoot,  // back with nothing to close
};

struct NavEvent {
    NavEventKind kind;
    char screen[scene::kNameCapacity];
};

struct OpenOptions {
    const char* key = nullptr;  // reuse an open instance with this key instead of opening another
    bool wait = false;          // start in `opening` until ready()
    // The call may only tighten what the screen file declares.
    bool force = false;         // not dismissible, whatever the file says
    bool unskippable = false;   // entrance cannot be skipped, whatever the file says
};

Navigator* create_navigator(Context* context);
// Closes nothing politely: everything is released at once.
void destroy_navigator(Navigator* nav);

// Loads a root screen (bottom to top in call order). False after diagnostics.
bool add_root(Navigator* nav, const char* path, DiagList* diags);
// Registers a screen that open() can open by its [screen] name. The file is loaded once to check
// it and learn the name. False after diagnostics.
bool add_catalog(Navigator* nav, const char* path, DiagList* diags);
bool in_catalog(const Navigator* nav, const char* name);
uint32_t catalog_names(const Navigator* nav, const char** names, uint32_t max);

// Opens a catalog screen on top. Returns false (with a diagnostic) if the name is unknown or the
// file fails to load.
bool open(Navigator* nav, const char* name, const OpenOptions& options, DiagList* diags);
// An opening screen with that name starts its entrance.
bool ready(Navigator* nav, const char* name);
// Closes the topmost opened screen with that name (an opening one is cancelled). False if none.
bool close(Navigator* nav, const char* name);
// Closes the opened screen that owns `screen` (a "close" button on it).
bool close_screen(Navigator* nav, const Screen* screen);
BackResult back(Navigator* nav, bool claimed);
void home(Navigator* nav);
void abort_all(Navigator* nav);

// Advances transitions by dt, then lays every screen out and gives each the input it may have,
// top first: screens that are not open get none, and an open modal hides the input from every
// screen below it. Navigation input (back) is not routed here; call back() for it.
void update(Navigator* nav, const UiInput& input, float width, float height, float dt);
// Draws roots and opened screens bottom to top, with their transitions and spinners.
void draw(Navigator* nav, DrawList* list);

// Every screen in the stack, roots first, bottom to top.
uint32_t entry_count(const Navigator* nav);
Screen* entry_screen(const Navigator* nav, uint32_t index);
scene::Scene* entry_document(const Navigator* nav, uint32_t index);
EntryState entry_state(const Navigator* nav, uint32_t index);
bool entry_is_root(const Navigator* nav, uint32_t index);
// Topmost entry with that name (closing ones last), or -1.
int find_entry(const Navigator* nav, const char* name);
// The topmost open screen that takes input: the one a back press is about. -1 if none.
int focus_owner(const Navigator* nav);
// True if any input this frame went to a screen (pointer over a surface, or a modal open).
bool input_captured(const Navigator* nav);

// Events since the last clear_events.
uint32_t event_count(const Navigator* nav);
const NavEvent* event_at(const Navigator* nav, uint32_t index);
void clear_events(Navigator* nav);

const char* entry_state_name(EntryState state);

}  // namespace zaro::ui
