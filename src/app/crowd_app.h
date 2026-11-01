// The crowd demo application: wires sim (model) to render (view) and owns the frame loop.
#pragma once

#include "rhi/rhi.h"

#include <stdint.h>

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
};

// Returns the process exit code.
int run_crowd(const RunOptions& options);

}  // namespace zaro::app
