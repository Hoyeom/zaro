// `zaro build` (D-027): one build pipeline for every target. A game build file (scene/build_spec.h)
// and its plugins' build extensions declare the steps; the stages run in a fixed order:
//
//   validate  the build file, the scene (as `zaro check` does), every extension and step
//   pre       the pre steps: plugins first, in scene order, then the game's
//   native    the target's native build                            (target only)
//   export    the target's project, e.g. `zaro export android`     (target only)
//   post      the post steps, after the export                     (target only)
//   package   the target's package, e.g. an AAB                    (target only)
//   sign      the signature check of the package                   (target only)
//
// Before the pre steps the pipeline writes <out>/build_info.toml: game, version, version code,
// commit, environment, release, debug_tools and the environment's config. Step arguments may name
// {out}, {env}, {version} and {version_code}; a step's own directory is its working directory.
#pragma once

#include <stdint.h>

namespace zaro::app {

struct BuildOptions {
    const char* build_file = nullptr;
    const char* environment = nullptr;   // default: the build file's first environment
    const char* version_code = nullptr;  // overrides [game] version_code (CI run numbers)
    const char* out_dir = nullptr;       // default build/zaro-build/<game>-<environment>
    bool plan = false;                   // print the stages and steps without running them
};

// A step built into the pipeline (`uses = "<name>"`). `args` are the step's `with` arguments,
// resolved against the file that declares the step. Returns 0 on success; errors are printed.
struct BuiltinStep {
    const char* name;
    const char* usage;  // the `with` arguments, for messages
    int (*run)(const char* const* args, uint32_t count, const char* game_scene);
};

// The built-in steps: check, test, design-check. A new one (P3's asset cooking) is one more entry
// in kBuiltinSteps in pipeline.cpp.
const BuiltinStep* builtin_steps(uint32_t* count);

// Runs the pipeline. Returns 0 on success, 1 when a stage fails, 2 on bad options.
int build_game(const BuildOptions& options);

}  // namespace zaro::app
