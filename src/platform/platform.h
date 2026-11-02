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
    // Pointer: the mouse, or a finger on a touch screen, in window pixels (origin top-left).
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
    bool pointer_down = false;      // held now
    bool pointer_pressed = false;   // went down since the previous poll
    bool pointer_released = false;  // went up since the previous poll
    bool pointer_touch = false;     // the pointer is a finger, which does not hover
    float wheel_y = 0.0f;           // wheel steps since the previous poll, + scrolls up
    // UI navigation since the previous poll: arrow keys or d-pad, Enter or Space or gamepad A
    // (confirm), Escape or Android back or gamepad B (back). The app decides what an unused back
    // does; the crowd demo quits.
    bool nav_up = false;
    bool nav_down = false;
    bool nav_left = false;
    bool nav_right = false;
    bool nav_confirm = false;
    bool nav_back = false;
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

// Pumps OS events and samples the keyboard and touch. Edge-triggered fields (pressed, released,
// wheel, nav) cover the events since the previous call. A finger on the screen also acts as a
// virtual stick for move_x/move_y: where it lands is the centre, and dragging it a tenth of the
// window's shorter side away is full speed. Keys win over touch.
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

// Runs a program directly, without a shell: argv[0] is the program (looked up on PATH), argv ends
// with nullptr. It runs in `cwd` (nullptr: this process's) with no stdin and this process's
// stderr. Its stdout is shared too, or, when `output` is given, collected there (at most
// output_size - 1 bytes, NUL-terminated). Returns the exit code, or -1 if it could not start.
int run_process(const char* const* argv, const char* cwd, char* output, uint32_t output_size);
// The value of an environment variable, or nullptr when it is unset.
const char* env_var(const char* name);

// Audio output (D-032): the default playback device, fed by a callback on the audio thread that
// writes `frames` interleaved float frames of the requested channel count. SDL converts to what the
// device takes. nullptr when there is no device (CI machines, a server); the caller then mixes
// nothing.
using AudioFn = void (*)(float* out, uint32_t frames, void* user);
struct AudioOutput;
AudioOutput* open_audio_output(uint32_t sample_rate, uint32_t channels, AudioFn fn, void* user);
void close_audio_output(AudioOutput* output);
// Stops calling the callback while paused (the mobile background).
void pause_audio_output(AudioOutput* output, bool paused);

// The directory where the game `game` keeps its files on this device, created if missing, with a
// trailing separator: the user's application data (Windows %APPDATA%\zaro\<game>\, Linux
// ~/.local/share/zaro/<game>/), or the app's internal storage on Android. False if there is none.
bool user_data_dir(const char* game, char* out, uint32_t size);
// A whole file in a buffer allocated with malloc and NUL-terminated; nullptr if it cannot be read.
char* read_file(const char* path, uint64_t* size);
// Replaces `path` so that a crash or a killed app leaves either the old or the new content: the data
// goes to <path>.tmp, is flushed to the disk, and is renamed over `path`. With `backup`, the old file
// is renamed to it first. False if any step failed (the old file is then still there or in `backup`).
bool write_file_atomic(const char* path, const void* data, uint64_t size, const char* backup);

using FileFn = void (*)(const char* path, void* user);
// Calls fn for `path` if it is a file ending in `suffix`, or for every such file below it if it is
// a directory (recursively; hidden entries and directories named "build" are skipped).
// Returns false if `path` does not exist.
bool find_files(const char* path, const char* suffix, FileFn fn, void* user);

}  // namespace zaro::platform
