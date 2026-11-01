// Platform layer: windows, input, time, files. The only code allowed to include SDL or OS
// headers, so ports (consoles, mobile) replace this directory and nothing else.
#pragma once

#include <stdint.h>

namespace zaro::platform {

// What the GPU backend needs to create a presentation surface.
struct NativeWindow {
    enum Kind { kNone, kWin32, kMetalLayer, kX11, kWayland } kind = kNone;
    void* display = nullptr;  // HINSTANCE, X11 Display*, wl_display*
    void* handle = nullptr;   // HWND, CAMetalLayer*, wl_surface*
    uint64_t x11_window = 0;
};

struct Window;

struct Input {
    bool quit = false;
    float move_x = 0.0f;  // -1..1, right is +x
    float move_y = 0.0f;  // -1..1, up (screen) is +y
};

bool init(bool headless);
void shutdown();

Window* create_window(const char* title, int width, int height);
void destroy_window(Window* window);
NativeWindow native_window(Window* window);
void window_pixel_size(Window* window, int* width, int* height);
void set_window_title(Window* window, const char* title);

// Pumps OS events and samples the keyboard.
void poll_input(Input* input);

uint64_t now_ticks();
double ticks_to_seconds(uint64_t ticks);

// Directory of the running executable, with a trailing separator.
const char* base_path();
// Modification time of a file in nanoseconds, or 0 if it does not exist.
uint64_t file_mtime(const char* path);

}  // namespace zaro::platform
