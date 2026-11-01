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

namespace {

// Window points to pixels for a mouse event's window.
float pixel_density(SDL_WindowID id) {
    SDL_Window* w = SDL_GetWindowFromID(id);
    const float d = w ? SDL_GetWindowPixelDensity(w) : 1.0f;
    return d > 0.0f ? d : 1.0f;
}

void on_key(Input* input, SDL_Keycode key) {
    switch (key) {
        case SDLK_UP: input->nav_up = true; break;
        case SDLK_DOWN: input->nav_down = true; break;
        case SDLK_LEFT: input->nav_left = true; break;
        case SDLK_RIGHT: input->nav_right = true; break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE: input->nav_confirm = true; break;
        case SDLK_ESCAPE:
        case SDLK_AC_BACK: input->nav_back = true; break;
        default: break;
    }
}

void on_gamepad_button(Input* input, uint8_t button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: input->nav_up = true; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: input->nav_down = true; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: input->nav_left = true; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: input->nav_right = true; break;
        case SDL_GAMEPAD_BUTTON_SOUTH: input->nav_confirm = true; break;
        case SDL_GAMEPAD_BUTTON_EAST: input->nav_back = true; break;
        default: break;
    }
}

}  // namespace

void poll_input(Input* input) {
    input->pointer_pressed = input->pointer_released = false;
    input->wheel_y = 0.0f;
    input->nav_up = input->nav_down = input->nav_left = input->nav_right = false;
    input->nav_confirm = input->nav_back = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT: input->quit = true; break;
            case SDL_EVENT_KEY_DOWN: on_key(input, e.key.key); break;
            case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: on_gamepad_button(input, e.gbutton.button); break;
            case SDL_EVENT_MOUSE_MOTION: {
                const float d = pixel_density(e.motion.windowID);
                input->pointer_x = e.motion.x * d;
                input->pointer_y = e.motion.y * d;
                input->pointer_touch = e.motion.which == SDL_TOUCH_MOUSEID;
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (e.button.button != SDL_BUTTON_LEFT) break;
                const float d = pixel_density(e.button.windowID);
                const bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                input->pointer_x = e.button.x * d;
                input->pointer_y = e.button.y * d;
                input->pointer_touch = e.button.which == SDL_TOUCH_MOUSEID;  // SDL turns touches into mouse events
                input->pointer_down = down;
                if (down) input->pointer_pressed = true;
                else input->pointer_released = true;
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: input->wheel_y += e.wheel.y; break;
            default: break;
        }
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

namespace {

struct FindCtx {
    const char* suffix;
    FileFn fn;
    void* user;
};

bool ends_with(const char* s, const char* suffix) {
    const size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

SDL_EnumerationResult SDLCALL find_cb(void* user, const char* dirname, const char* fname) {
    const FindCtx* ctx = (const FindCtx*)user;
    if (fname[0] == '.' || strcmp(fname, "build") == 0) return SDL_ENUM_CONTINUE;
    char path[1024];
    SDL_snprintf(path, sizeof(path), "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        SDL_EnumerateDirectory(path, find_cb, user);
    } else if (info.type == SDL_PATHTYPE_FILE && ends_with(fname, ctx->suffix)) {
        // Report '/' separators on every OS so output and JSON look the same everywhere.
        for (char* c = path; *c; ++c) {
            if (*c == '\\') *c = '/';
        }
        ctx->fn(path, ctx->user);
    }
    return SDL_ENUM_CONTINUE;
}

}  // namespace

bool find_files(const char* path, const char* suffix, FileFn fn, void* user) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return false;
    if (info.type == SDL_PATHTYPE_FILE) {
        if (ends_with(path, suffix)) fn(path, user);
        return true;
    }
    FindCtx ctx{suffix, fn, user};
    return SDL_EnumerateDirectory(path, find_cb, &ctx);
}

uint64_t file_mtime(const char* path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return 0;
    return (uint64_t)info.modify_time;
}

}  // namespace zaro::platform
