#include "platform/platform.h"

#include "core/log.h"

#include <SDL3/SDL.h>
#if defined(__APPLE__)
#include <SDL3/SDL_metal.h>
#endif

#include <string.h>

namespace zaro::platform {

struct Window {
    SDL_Window* sdl = nullptr;
#if defined(__APPLE__)
    SDL_MetalView metal_view = nullptr;
#endif
};

bool init(bool headless) {
    const SDL_InitFlags flags = headless ? 0 : (SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    if (!SDL_Init(flags)) {
        log_error(kErrPlatformInit, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

void shutdown() { SDL_Quit(); }

Window* create_window(const char* title, int width, int height) {
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL;
#endif
    SDL_Window* sdl = SDL_CreateWindow(title, width, height, flags);
    if (!sdl) {
        log_error(kErrPlatformInit, "SDL_CreateWindow failed: %s", SDL_GetError());
        return nullptr;
    }
    Window* w = new Window();
    w->sdl = sdl;
#if defined(__APPLE__)
    w->metal_view = SDL_Metal_CreateView(sdl);
#endif
    return w;
}

void destroy_window(Window* window) {
    if (!window) return;
#if defined(__APPLE__)
    if (window->metal_view) SDL_Metal_DestroyView(window->metal_view);
#endif
    SDL_DestroyWindow(window->sdl);
    delete window;
}

NativeWindow native_window(Window* window) {
    NativeWindow n;
    const SDL_PropertiesID props = SDL_GetWindowProperties(window->sdl);
#if defined(_WIN32)
    n.kind = NativeWindow::kWin32;
    n.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
    n.handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    (void)props;
    n.kind = NativeWindow::kMetalLayer;
    n.handle = SDL_Metal_GetLayer(window->metal_view);
#else
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && strcmp(driver, "wayland") == 0) {
        n.kind = NativeWindow::kWayland;
        n.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        n.handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        n.kind = NativeWindow::kX11;
        n.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        n.x11_window = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    }
#endif
    return n;
}

void window_pixel_size(Window* window, int* width, int* height) {
    SDL_GetWindowSizeInPixels(window->sdl, width, height);
}

void set_window_title(Window* window, const char* title) { SDL_SetWindowTitle(window->sdl, title); }

void poll_input(Input* input) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) input->quit = true;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) input->quit = true;
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float x = 0.0f, y = 0.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) x -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) x += 1.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) y += 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) y -= 1.0f;
    input->move_x = x;
    input->move_y = y;
}

uint64_t now_ticks() { return SDL_GetPerformanceCounter(); }

double ticks_to_seconds(uint64_t ticks) {
    return (double)ticks / (double)SDL_GetPerformanceFrequency();
}

const char* base_path() {
    const char* p = SDL_GetBasePath();
    return p ? p : "./";
}

uint64_t file_mtime(const char* path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return 0;
    return (uint64_t)info.modify_time;
}

}  // namespace zaro::platform
