// The crowd demo application: wires sim (model) to render (view) and owns the frame loop.
#pragma once

#include "rhi/rhi.h"

#include <stdint.h>

namespace zaro {
struct DiagList;
namespace scene {
struct Scene;
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
};

// Returns the process exit code.
int run_crowd(const RunOptions& options);

struct SimulateResult {
    uint64_t state_hash = 0;
    uint64_t ticks = 0;
};

// Runs a scene for `ticks` ticks without a window or GPU, with the scripted player input and the
// scene's script (used by `zaro test`). Errors go to diags. *scene_out receives the scene as the
// run left it (values a script changed included), or nullptr; free it with scene::free_scene.
// Returns false if the scene, the script or a tick failed.
bool simulate_scene(const RunOptions& options, uint32_t ticks, DiagList* diags, scene::Scene** scene_out,
                    SimulateResult* out);

}  // namespace zaro::app
