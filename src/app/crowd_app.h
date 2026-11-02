// The crowd demo application: wires sim (model) to render (view) and owns the frame loop.
#pragma once

#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro {
struct DiagList;
namespace scene {
struct Scene;
struct TestSpec;
}
}  // namespace zaro

namespace zaro::app {

struct RunOptions {
    bool headless = false;
    bool sim_only = false;     // headless without a GPU: simulation and state hash only
    uint32_t frames = 600;     // measured frames (headless)
    uint32_t warmup = 60;      // frames run before measuring (headless)
    const char* scene_path = nullptr;  // TOML scene; nullptr runs the built-in crowd defaults
    uint32_t enemies = 50000;
    uint64_t seed = 1;
    bool enemies_set = false;  // --enemies / --seed given: override the scene's values
    bool seed_set = false;
    uint32_t threads = 0;      // 0 = hardware threads
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool vsync = true;
    float zoom = 1.0f;
    rhi::Backend backend = rhi::Backend::kDefault;
    const char* perf_path = nullptr;        // write perf.json here
    const char* screenshot_path = nullptr;  // headless: PNG of the last frame
    const char* language = nullptr;         // UI language; nullptr = the scene's first
    // The game's save (D-033): kept in this directory; without one, windowed runs use the game's user
    // data directory and headless runs keep it in memory, starting from save_start (or the defaults).
    const char* save_dir = nullptr;
    const char* save_start = nullptr;
    // Headless: buttons clicked on the first frames, one per frame, as "screen/node".
    static constexpr uint32_t kMaxClicks = 8;
    const char* clicks[kMaxClicks] = {};
    uint32_t click_count = 0;
};

// Returns the process exit code.
int run_crowd(const RunOptions& options);

// Checks the scene's crowd components: a CrowdSpawner whose count is above its max is ZE1400
// (D-034). Returns false after adding diagnostics. `zaro check` and every run call it.
bool check_crowd(const scene::Scene* scene, DiagList* diags);

struct SimulateResult {
    uint64_t state_hash = 0;
    uint64_t ticks = 0;
};

struct UiLayer;
struct AudioLayer;
struct SaveLayer;

// Runs a scene for `ticks` ticks without a window or GPU, with the scripted player input, the
// scene's script and its UI (laid out at the options' size). `spec`, if given, supplies [[click]]s.
// Errors go to diags. *scene_out and *ui_out receive the scene and its UI as the run left them, or
// nullptr; free them with scene::free_scene and destroy_ui_layer (app/ui_layer.h). With audio_out,
// *audio_out receives the scene's sounds with their play counts (a null mixer), or nullptr; free it
// with destroy_audio_layer (app/audio_layer.h). With save_out, *save_out receives the game's save
// as committed (in memory), or nullptr; free it with destroy_save_layer (app/save_layer.h). Returns
// false if the scene, the script, a click or a tick failed.
bool simulate_scene(const RunOptions& options, uint32_t ticks, const scene::TestSpec* spec, DiagList* diags,
                    scene::Scene** scene_out, UiLayer** ui_out, SimulateResult* out,
                    AudioLayer** audio_out = nullptr, SaveLayer** save_out = nullptr);

}  // namespace zaro::app
