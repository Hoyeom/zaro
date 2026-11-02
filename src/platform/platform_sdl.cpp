#include "platform/platform.h"

#include "core/log.h"

#include <SDL3/SDL.h>
#if defined(__APPLE__)
#include <SDL3/SDL_metal.h>
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>

namespace zaro::platform {

namespace {

#if defined(__ANDROID__)
char g_data_dir[512] = "./";

// Android drops stdout and stderr; forward them to logcat line by line.
void* logcat_pump(void* user) {
    const int fd = (int)(intptr_t)user;
    char buf[1024];
    size_t len = 0;
    for (;;) {
        const ssize_t n = read(fd, buf + len, sizeof(buf) - 1 - len);
        if (n <= 0) break;
        len += (size_t)n;
        size_t start = 0;
        for (size_t i = 0; i < len; ++i) {
            if (buf[i] != '\n') continue;
            buf[i] = 0;
            __android_log_write(ANDROID_LOG_INFO, "zaro", buf + start);
            start = i + 1;
        }
        if (start == 0 && len == sizeof(buf) - 1) {  // a line longer than the buffer
            buf[len] = 0;
            __android_log_write(ANDROID_LOG_INFO, "zaro", buf);
            len = 0;
        } else {
            memmove(buf, buf + start, len - start);
            len -= start;
        }
    }
    return nullptr;
}

void redirect_stdio_to_logcat() {
    int fds[2];
    if (pipe(fds) != 0) return;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    pthread_t thread;
    if (pthread_create(&thread, nullptr, logcat_pump, (void*)(intptr_t)fds[0]) == 0) pthread_detach(thread);
}

// Copies every file named in assets/zaro_assets.txt (one path per line) from the APK into dir.
// A relative SDL path on Android reads the internal files directory first and the APK second,
// which is why dir is never the internal files directory itself.
bool unpack_assets(const char* dir) {
    size_t size = 0;
    char* list = (char*)SDL_LoadFile("zaro_assets.txt", &size);
    if (!list) {
        log_error(kErrPlatformInit, "zaro_assets.txt: not in the APK (%s); export with 'zaro export android'",
                  SDL_GetError());
        return false;
    }
    bool ok = true;
    for (char* line = list; line && *line;) {
        char* end = strchr(line, '\n');
        if (end) *end = 0;
        const size_t n = strlen(line);
        if (n && line[n - 1] == '\r') line[n - 1] = 0;
        if (line[0]) {
            char out[1024];
            snprintf(out, sizeof(out), "%s%s", dir, line);
            if (char* slash = strrchr(out, '/')) {
                *slash = 0;
                SDL_CreateDirectory(out);
                *slash = '/';
            }
            size_t bytes = 0;
            void* data = SDL_LoadFile(line, &bytes);
            if (!data || !SDL_SaveFile(out, data, bytes)) {
                log_error(kErrPlatformInit, "%s: cannot unpack from the APK: %s", line, SDL_GetError());
                ok = false;
            }
            SDL_free(data);
        }
        line = end ? end + 1 : nullptr;
    }
    SDL_free(list);
    return ok;
}
#endif

// Touch as a virtual stick (see poll_input in platform.h), in window coordinates.
struct TouchStick {
    bool active = false;
    SDL_FingerID finger = 0;
    float width = 1.0f, height = 1.0f;  // window size when the finger landed
    float radius = 1.0f;                // drag distance for full speed
    float origin_x = 0.0f, origin_y = 0.0f;
    float x = 0.0f, y = 0.0f;
};
TouchStick g_stick;

}  // namespace

#if defined(__ANDROID__)
bool prepare_process() {
    redirect_stdio_to_logcat();
    // External files (/sdcard/Android/data/<package>/files) can be read with `adb pull`.
    if (const char* external = SDL_GetAndroidExternalStoragePath()) {
        snprintf(g_data_dir, sizeof(g_data_dir), "%s/", external);
    } else if (const char* internal = SDL_GetAndroidInternalStoragePath()) {
        snprintf(g_data_dir, sizeof(g_data_dir), "%s/zaro/", internal);
    } else {
        log_error(kErrPlatformInit, "no Android files directory: %s", SDL_GetError());
        return false;
    }
    SDL_CreateDirectory(g_data_dir);
    if (!unpack_assets(g_data_dir)) return false;
    if (chdir(g_data_dir) != 0) {
        log_error(kErrPlatformInit, "cannot enter %s", g_data_dir);
        return false;
    }
    return true;
}
#else
bool prepare_process() { return true; }
#endif

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

struct AudioOutput {
    SDL_AudioStream* stream = nullptr;
    AudioFn fn = nullptr;
    void* user = nullptr;
    uint32_t channels = 2;
    float buffer[2048 * 8];  // one chunk of frames for the callback
};

namespace {

// SDL asks for `additional` more bytes; the mixer writes them in chunks.
void SDLCALL feed_audio(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    AudioOutput* o = (AudioOutput*)user;
    const uint32_t frame_bytes = (uint32_t)sizeof(float) * o->channels;
    const uint32_t max_frames = (uint32_t)(sizeof(o->buffer) / frame_bytes);
    uint32_t frames = additional > 0 ? ((uint32_t)additional + frame_bytes - 1) / frame_bytes : 0;
    while (frames) {
        const uint32_t n = frames < max_frames ? frames : max_frames;
        o->fn(o->buffer, n, o->user);
        SDL_PutAudioStreamData(stream, o->buffer, (int)(n * frame_bytes));
        frames -= n;
    }
}

}  // namespace

AudioOutput* open_audio_output(uint32_t sample_rate, uint32_t channels, AudioFn fn, void* user) {
    if (!channels || channels > 8) return nullptr;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log_info("no audio output (%s); sound is off", SDL_GetError());
        return nullptr;
    }
    AudioOutput* o = new AudioOutput();
    o->fn = fn;
    o->user = user;
    o->channels = channels;
    const SDL_AudioSpec spec = {SDL_AUDIO_F32, (int)channels, (int)sample_rate};
    o->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed_audio, o);
    if (!o->stream) {
        log_info("no audio output (%s); sound is off", SDL_GetError());
        delete o;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return nullptr;
    }
    SDL_ResumeAudioStreamDevice(o->stream);
    return o;
}

void close_audio_output(AudioOutput* o) {
    if (!o) return;
    SDL_DestroyAudioStream(o->stream);  // also closes the device; the callback has returned
    delete o;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void pause_audio_output(AudioOutput* o, bool paused) {
    if (!o) return;
    if (paused) SDL_PauseAudioStreamDevice(o->stream);
    else SDL_ResumeAudioStreamDevice(o->stream);
}

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
#elif defined(__ANDROID__)
    n.kind = NativeWindow::kAndroid;
    n.handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
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
    input->window_changed = false;
    input->pointer_pressed = input->pointer_released = false;
    input->wheel_y = 0.0f;
    input->nav_up = input->nav_down = input->nav_left = input->nav_right = false;
    input->nav_confirm = input->nav_back = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_TERMINATING: input->quit = true; break;
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
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
                input->background = true;
                g_stick.active = false;
                break;
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                input->background = false;
                input->window_changed = true;
                break;
            case SDL_EVENT_FINGER_DOWN:
                if (!g_stick.active) {
                    int w = 0, h = 0;
                    if (SDL_Window* win = SDL_GetWindowFromID(e.tfinger.windowID)) SDL_GetWindowSize(win, &w, &h);
                    if (w <= 0 || h <= 0) w = h = 1;
                    g_stick.active = true;
                    g_stick.finger = e.tfinger.fingerID;
                    g_stick.width = (float)w;
                    g_stick.height = (float)h;
                    g_stick.radius = 0.1f * (float)(w < h ? w : h);
                    g_stick.origin_x = g_stick.x = e.tfinger.x * (float)w;
                    g_stick.origin_y = g_stick.y = e.tfinger.y * (float)h;
                }
                break;
            case SDL_EVENT_FINGER_MOTION:
                if (g_stick.active && e.tfinger.fingerID == g_stick.finger) {
                    g_stick.x = e.tfinger.x * g_stick.width;
                    g_stick.y = e.tfinger.y * g_stick.height;
                }
                break;
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                if (e.tfinger.fingerID == g_stick.finger) g_stick.active = false;
                break;
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
    if (x == 0.0f && y == 0.0f && g_stick.active) {
        x = (g_stick.x - g_stick.origin_x) / g_stick.radius;
        y = -(g_stick.y - g_stick.origin_y) / g_stick.radius;  // screen y grows downward
        const float len2 = x * x + y * y;
        if (len2 > 1.0f) {
            const float inv = 1.0f / SDL_sqrtf(len2);
            x *= inv;
            y *= inv;
        }
    }
    input->move_x = x;
    input->move_y = y;
}

uint64_t now_ticks() { return SDL_GetPerformanceCounter(); }

double ticks_to_seconds(uint64_t ticks) {
    return (double)ticks / (double)SDL_GetPerformanceFrequency();
}

const char* base_path() {
#if defined(__ANDROID__)
    return g_data_dir;
#else
    const char* p = SDL_GetBasePath();
    return p ? p : "./";
#endif
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

bool create_directories(const char* path) { return SDL_CreateDirectory(path); }

bool copy_file(const char* from, const char* to) { return SDL_CopyFile(from, to); }

bool user_data_dir(const char* game, char* out, uint32_t size) {
    char* dir = SDL_GetPrefPath("zaro", game);  // created by SDL, ends with a separator
    if (!dir) return false;
    const int n = snprintf(out, size, "%s", dir);
    SDL_free(dir);
    return n > 0 && (uint32_t)n < size;
}

char* read_file(const char* path, uint64_t* size) {
    size_t n = 0;
    void* data = SDL_LoadFile(path, &n);  // NUL-terminated
    if (!data) return nullptr;
    char* out = (char*)malloc(n + 1);
    memcpy(out, data, n + 1);
    SDL_free(data);
    if (size) *size = n;
    return out;
}

bool write_file_atomic(const char* path, const void* data, uint64_t size, const char* backup) {
    char tmp[1100];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return false;
    SDL_IOStream* io = SDL_IOFromFile(tmp, "wb");
    if (!io) return false;
    // SDL_FlushIO reaches the disk (FlushFileBuffers, fdatasync), so the rename never exposes a
    // half-written file.
    const bool written = SDL_WriteIO(io, data, (size_t)size) == (size_t)size && SDL_FlushIO(io);
    if (!SDL_CloseIO(io) || !written) {
        SDL_RemovePath(tmp);
        return false;
    }
    SDL_PathInfo info;
    if (backup && SDL_GetPathInfo(path, &info) && !SDL_RenamePath(path, backup)) return false;
    return SDL_RenamePath(tmp, path);
}

const char* env_var(const char* name) { return SDL_getenv(name); }

int run_process(const char* const* argv, const char* cwd, char* output, uint32_t output_size) {
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)argv);
    if (cwd) SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                          output ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_INHERITED);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!process) return -1;
    int exit_code = -1;
    if (output) {
        size_t size = 0;
        char* data = (char*)SDL_ReadProcess(process, &size, &exit_code);
        const size_t n = data ? (size < output_size ? size : output_size - 1) : 0;
        if (output_size) {
            if (n) memcpy(output, data, n);
            output[n] = 0;
        }
        SDL_free(data);
    } else {
        SDL_WaitProcess(process, true, &exit_code);
    }
    SDL_DestroyProcess(process);
    return exit_code;
}

}  // namespace zaro::platform
