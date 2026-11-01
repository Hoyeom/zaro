// Platform layer: windows, input, time, files. The only code allowed to include SDL or OS
// headers, so ports (consoles, mobile) replace this directory and nothing else.
#pragma once

#include <stdint.h>

namespace zaro::platform {

// What the GPU backend needs to create a presentation surface.
struct NativeWindow {
    enum Kind { kNone, kWin32, kMetalLayer, kX11, kWayland, kAndroid } kind = kNone;
    void* display = nullptr;  // HINSTANCE, X11 Display*, wl_display*
    void* handle = nullptr;   // HWND, CAMetalLayer*, wl_surface*, ANativeWindow*
    uint64_t x11_window = 0;
};

struct Window;

struct Input {
    bool quit = false;
    float move_x = 0.0f;  // -1..1, right is +x
    float move_y = 0.0f;  // -1..1, up (screen) is +y
    // Mobile lifecycle. While in the background the window has no surface: do not render.
    // window_changed is set for one poll when the app returns: recreate the GPU surface from
    // native_window(), which may be a new OS window.
    bool background = false;
    bool window_changed = false;
};

// Process setup before any file is read. Android: sends stdout/stderr to logcat (tag "zaro"),
// unpacks the files listed in the APK's assets/zaro_assets.txt into the app's external files
// directory, and makes that the working directory. Elsewhere it does nothing.
bool prepare_process();

bool init(bool headless);
void shutdown();

Window* create_window(const char* title, int width, int height);
void destroy_window(Window* window);
NativeWindow native_window(Window* window);
void window_pixel_size(Window* window, int* width, int* height);
void set_window_title(Window* window, const char* title);

// Pumps OS events and samples the keyboard and touch. A finger on the screen acts as a virtual
// stick: where it lands is the centre, and dragging it a tenth of the window's shorter side away
// is full speed. Keys win over touch. Escape and Android's back button quit.
void poll_input(Input* input);

uint64_t now_ticks();
double ticks_to_seconds(uint64_t ticks);

// Directory of the running executable, with a trailing separator. Android: the directory
// prepare_process() unpacked the assets into.
const char* base_path();
// Modification time of a file in nanoseconds, or 0 if it does not exist.
uint64_t file_mtime(const char* path);
// Creates a directory and any missing parents. True if it exists afterwards.
bool create_directories(const char* path);
// Copies a file, replacing `to`. The parent directory of `to` must exist.
bool copy_file(const char* from, const char* to);

using FileFn = void (*)(const char* path, void* user);
// Calls fn for `path` if it is a file ending in `suffix`, or for every such file below it if it is
// a directory (recursively; hidden entries and directories named "build" are skipped).
// Returns false if `path` does not exist.
bool find_files(const char* path, const char* suffix, FileFn fn, void* user);

}  // namespace zaro::platform
