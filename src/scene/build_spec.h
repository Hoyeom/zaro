// Game build files (D-027): how `zaro build` turns a game into a build for a target.
//
//   [game]
//   name = "crowd"                     # required
//   scene = "scene.toml"               # required: the scene the game starts with
//   version = "1.4.0"                  # required: the version players see
//   version_code = 12                  # optional, 1..2100000000; `zaro build --version-code` overrides
//
//   [environment.dev]                  # at least one; `zaro build --env` picks one (default: the first)
//   debug_tools = true                 # optional, default false
//   [environment.dev.config]           # optional strings, numbers and booleans for the game
//   api = "https://dev.example.com"
//
//   [environment.production]
//   release = true                     # a release environment: debug_tools cannot be true and no
//                                      # config key may start with "debug" (ZE1002)
//
//   [[step]]
//   stage = "pre"                      # "pre" (before the native build) or "post" (after the export)
//   name = "client"                    # unique among the game's and its plugins' steps
//   run = ["python", "gen/client.py", "--out", "gen"]   # a program and its arguments
//
//   [[step]]
//   stage = "pre"
//   name = "tests"
//   uses = "test"                      # a step built into zaro build instead of a program
//   with = ["tests"]                   # its arguments
//
// A step has either `run` or `uses`. `run` starts the program directly, without a shell, in the
// directory of the file that declares the step; a shell (sh, bash, cmd, powershell, ...) or an
// interpreter's inline-code flag (-c, -e) is refused, so what runs is what the file says. The
// first word "zaro" means the running zaro binary. Paths in `with` are relative to the declaring
// file. Plugins add steps the same way in their build extension (scene/build_extension.h); in
// each stage the plugins' steps run first, in the order the scene lists them, then the game's.
//
// Stages run in this order: validate, pre, native, export, post, package, sign. Every problem is a
// diagnostic with file and line: ZE1000 for the build file, ZE1001 for a failed step, ZE1002 for a
// release environment that enables debugging.
#pragma once

#include "core/diag.h"

#include <stdint.h>

namespace zaro::scene {

constexpr uint32_t kMaxBuildSteps = 32;      // per file
constexpr uint32_t kMaxStepArgs = 16;
constexpr uint32_t kStepArgCapacity = 260;
constexpr uint32_t kMaxEnvironments = 8;
constexpr uint32_t kMaxConfigEntries = 64;

enum class BuildStage : uint8_t { kPre, kPost };

struct BuildStep {
    char name[64];
    BuildStage stage;
    char uses[32];                               // a built-in step, or empty for `run`
    char args[kMaxStepArgs][kStepArgCapacity];   // `run` (program first) or `with`
    uint32_t arg_count;
    char file[260];                              // the file that declares the step
    int line;
};

enum class ConfigType : uint8_t { kString, kNumber, kBool };

struct ConfigEntry {
    char key[64];
    char value[256];  // as text: the string, the number, or true/false
    ConfigType type;
    int line;
};

struct BuildEnvironment {
    char name[32];
    bool release;
    bool debug_tools;
    ConfigEntry config[kMaxConfigEntries];
    uint32_t config_count;
    int line;
};

struct BuildSpec {
    char path[260];
    char name[64];
    char scene[260];  // resolved path
    int scene_line;
    char version[64];
    uint32_t version_code;  // 0 when not given
    BuildEnvironment environments[kMaxEnvironments];
    uint32_t environment_count;
    BuildStep steps[kMaxBuildSteps];
    uint32_t step_count;
};

// Loads and validates a game build file. Problems are diagnostics with file and line; returns
// nullptr if there was any. The scene itself is not loaded here.
BuildSpec* load_build_spec(const char* path, DiagList* diags);
void free_build_spec(BuildSpec* spec);

const BuildEnvironment* find_environment(const BuildSpec* spec, const char* name);

}  // namespace zaro::scene
