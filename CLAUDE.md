# ZARO — agent and contributor guide

ZARO (**Z**ero-royalty, **A**I-native, **R**apid, **O**pen) is a royalty-free game engine built
to be driven by AI agents: text assets, a headless CLI, structured errors. It targets very large
entity counts (tens of thousands of enemies) with compute-shader-heavy rendering.
Every architectural decision and its reasoning is in [docs/DECISIONS.md](docs/DECISIONS.md); the
phases sessions pick work from are in [docs/ROADMAP.md](docs/ROADMAP.md).

## Commands

| Task | Command |
|---|---|
| Configure + build (debug) | `python tools/build.py` |
| Build + run all tests | `python tools/build.py debug --test` |
| Tests without a GPU (as CI runs them) | `python tools/build.py debug --test --no-gpu-tests` |
| Engine compile-time budget | `python tools/build.py debug --engine-budget=60` (prints `engine_build_seconds`) |
| Linux build in Docker (from Windows too) | `sh tools/ci/linux-docker.sh` (`--clang` to catch Clang-only warnings) |
| Clean rebuild | `python tools/build.py debug --clean` |
| Release / Tracy builds | `python tools/build.py release`, `python tools/build.py profile` |
| Lint the whole tree | `python tools/lint.py` (rules: `--list-rules`) |
| Run a scene in a window | `build/debug/zaro run examples/crowd/scene.toml` (WASD or arrows, Esc quits) |
| Headless benchmark | `build/release/zaro run examples/crowd/scene.toml --headless --perf perf.json` |
| Headless screenshot | `build/debug/zaro run --headless --frames 60 --screenshot shot.png` |
| Simulation only, no GPU | `build/debug/zaro run examples/crowd/scene.toml --sim-only --frames 300` |
| List components (fields, types, defaults, ranges) as JSON | `build/debug/zaro schema` |
| Validate a scene without running it | `build/debug/zaro check examples/crowd/scene.toml [--json]` |
| Validate a scene's screens and strings, or one screen | `build/debug/zaro check examples/hello/scene.toml`, `... check examples/hello/ui/hud.toml` |
| Run scene tests (no GPU) | `build/debug/zaro test examples/crowd/tests [--json]` |
| Accept a changed simulation as the new golden | `build/debug/zaro test <paths> --update` |
| Render a scene to PNG | `build/debug/zaro screenshot examples/crowd/scene.toml out.png [--frames N]` |
| Render a scene's UI in another language | `build/debug/zaro screenshot examples/hello/scene.toml out.png --lang en` |
| Cook a scene's SVG sprite art into PNG atlases (runs do it on demand) | `build/debug/zaro cook examples/cats/scene.toml [--force]` |
| Turn a Claude Design kit into a UI theme | `build/debug/zaro import-design examples/hello/design examples/hello/ui/kit [--check]` |
| All `run` options | `build/debug/zaro help` |
| Install the pinned Android toolchain (once) | `python tools/android.py setup` (`doctor` shows what is found) |
| Android native build (`libmain.so` + runtime package) | `python tools/build.py android` |
| Gradle project for an APK | `build/debug/zaro export android examples/crowd/scene.toml [--out DIR]` |
| APK in one step (native build, export, Gradle) | `python tools/android.py apk examples/crowd/scene.toml` |
| Install and start on a device / benchmark it | `python tools/android.py run`, `python tools/android.py perf [--enemies N]` |
| Google Play App Bundle through `zaro build` (upload key from the environment) | `python tools/android.py aab <build.toml> --env <release env> --release --version-code N` |
| Run a game's build pipeline / only validate and show it | `build/debug/zaro build examples/crowd/build.toml [--env NAME] [--target android]`, `... --plan` |
| Enable the git hooks (once per clone) | `git config core.hooksPath tools/githooks` |

`tools/build.py` wraps the presets in `CMakePresets.json` (`debug`, `release`, `profile`).
On Windows it finds MSVC through `vswhere`, falling back to a scan for `vcvars64.bat`, and adds
the CMake/Ninja bundled with Visual Studio to `PATH`. Nothing needs to be on `PATH` beforehand.
Its last output line is `build_seconds=<n>`.

The first configure downloads pinned dependencies into `.cache/deps/` (see
`cmake/ZaroDeps.cmake`). All presets share the downloads and sources there; dependencies built
from source compile into each preset's own `build/<preset>/_deps/`.

## Layout

```
include/zaro/   public C API = plugin ABI (C only, lint rules Z001/Z006)
src/<module>/   engine modules, see "Modules"
shaders/        Slang sources; the build compiles them to <bin>/shaders/*.wgsl
examples/       example scenes, prefabs, scripts and scene tests
tests/          test executables and tests/data/; c_abi_check.c compiles public headers as C
tools/          build.py, lint.py, git_rules.py, githooks/, ci/, github/
cmake/          ZaroCompilerFlags.cmake (zaro_target_defaults), ZaroDeps.cmake (pinned deps)
docs/           DECISIONS.md, ROADMAP.md, guide.md (walkthrough)
.claude/        settings.json (hooks, denied commands) and skills/
```

## Modules

Each directory under `src/` is one module and one static library. It may include only the
modules listed for it (lint rule Z007, `MODULE_DEPS` in tools/lint.py). Adding a module, header
ownership and the rest of the layering rules are in the `zaro-code` skill.

| Module | Role | May include |
|---|---|---|
| `core` | math, rng, hashing, jobs, logging, diagnostics, reflection, PNG; no platform or GPU | core |
| `platform` | windows, input, time, files; the only module that includes SDL | core |
| `rhi` | GPU interface `rhi.h`; `rhi_wgpu.cpp` is the only file that includes webgpu.h | core, platform |
| `sim` | gameplay state (the model), deterministic, headless-testable | core |
| `scene` | loads TOML scenes and prefabs into registered components; the only module that includes tomlc17 | core |
| `script` | sandboxed Luau scripts acting on a scene through reflection; the only module that includes Luau | core, scene |
| `render` | draws plain data (the view); never sees sim types | core, rhi |
| `ui` | game UI: fonts, text layout, glyph atlas, screens, widgets; no GPU | core, scene |
| `cook` | build-time asset cooking: SVG art into sprite atlases; the only module that includes LunaSVG; not built for Android (D-028) | core, scene |
| `app` | owns the frame loop and wires sim, sprites and ui to render | all of the above |
| `cli` | the `zaro` command | core, app |

## Skills

Load a skill before working in the situation it names; it holds what would otherwise be here.

- **`zaro-code`**: engine C++ and shaders: module layering, the C++ subset, deterministic
  parallel simulation, Slang shaders, the Windows header-dependency repair.
- **`zaro-scene`**: scenes, prefabs and components, Luau scripts, scene tests and golden hashes,
  headless runs and perf.json.
- **`zaro-ui`**: text and the glyph atlas, screen files, widgets, string tables, and how a frame's
  UI is built and drawn.
- **`zaro-fx`**: the neon look's visual effects: glowing shapes, trails and hit flash, the HDR
  frame and bloom, and how effect components reach render without touching the simulation.
- **`zaro-build`**: the build pipeline (`zaro build`): stages, game build files, environments,
  pre and post steps from games and plugins, built-in steps, and the release rule.
- **`zaro-android`**: the Android toolchain, the `android` preset, APK export, running on a
  device, and Google Play release bundles with their signing boundary.
- **`zaro-docs`**: how documentation is written in this public repository and where each fact goes.
- **`zaro-git`**: branches, commits, pull requests, auto-merge and CI. **Load it before writing a
  commit message.**

## Design principles (do not erode these)

- **Text-first assets**: scenes, prefabs and materials are text (TOML) referenced by path, with
  no hidden GUIDs. Derived binaries go in a gitignored cache (D-008).
- **CLI and headless first**: anything the editor can do, `zaro <command>` can do without a
  window, with JSON output on request. The editor is a UI on top of the CLI.
- **Errors an agent can act on**: a stable `ZEnnnn` id plus file and line when known. Component
  schemas and reflection data will be exportable.
- **Deterministic simulation** (D-005): gameplay state lives on the CPU in a fixed 60 Hz tick.
  The GPU produces visuals only.
- **Compute shaders are required**: there is no fallback path for GPUs without compute.
- **Sandboxed scripts**: game logic runs in Luau with no file or OS access (D-009).
- **Git-friendly and revertible**: one PR per change, squash-merged (D-015).
- **Stable API**: the plugin ABI is versioned (D-011). Prefer adding over changing.
- **Platform isolation**: OS and SDK headers stay inside `platform` and `rhi`, so console ports
  live in private repositories.
- **No proprietary SDKs in the core**: Firebase, IAP, ads and Steamworks are plugins in separate
  repositories.
- **A local MCP server** comes later. It accepts local connections only, with access limits.

## Rules that are enforced

Breaking a rule fails something. Each rule is listed with whatever catches it. A new rule gets a
check and a row here; a rule written only in prose is not followed.

| Rule | Enforced by |
|---|---|
| No exceptions, no RTTI | `/EHs-c- /GR-` or `-fno-exceptions -fno-rtti` plus `/WX` or `-Werror` in `zaro_target_defaults`; lint Z003 |
| Warnings are errors on ZARO targets | `/W4 /WX`, `-Wall -Wextra -Werror` |
| Public headers are plain C with an `extern "C"` guard | lint Z001, Z006; `zaro_c_abi_check` builds them as C |
| No heavy standard headers (`<iostream>`, `<format>`, `<functional>`, `<filesystem>`, `<regex>`, …) | lint Z002 (full list: `BANNED_HEADERS` in tools/lint.py) |
| Headers stay light: at most 12 includes, `#pragma once` | lint Z004, Z005 |
| Module layering and third-party header ownership | lint Z007 |
| Every skill is well-formed and listed under "Skills" above | lint Z008 |
| No keystores, signing passwords or keystore paths in the repository (D-025) | lint Z009 |
| Packed Android libraries: no build-machine paths, 16 KB LOAD alignment | `cmake/ZaroCheckElf.cmake` in the `android` build |
| Every dependency in `cmake/ZaroDeps.cmake` is listed with its license in `THIRD_PARTY_NOTICES.md` | lint Z010 |
| Simulation is deterministic across thread counts | `crowd_determinism` test |
| Golden state hashes hold on Windows, Linux and macOS | scene tests in CI |
| Branch names, commit messages, sign-off, no commits or pushes to `main` | `tools/git_rules.py` from the git hooks and CI; `permissions.deny` in `.claude/settings.json` |

Lint runs in three places:
1. The Claude Code `PostToolUse` hook in `.claude/settings.json`, after every Edit or Write.
   A violation exits 2 and the agent sees the diagnostic immediately.
2. The git pre-commit hook, `tools/githooks/pre-commit`.
3. The `lint` test in ctest, and the CI `rules` job.

A single line can opt out with `// zaro-lint: allow Z00N <reason>`. Without a reason the
opt-out is ignored.

## Don't

These bite before you know you are in the situation, which is why they are not in a skill.

- **Don't write anything about a private repository here.** This repository and its history are
  public. The owner's backend platform, its plugin and proprietary SDKs are described only in
  ZARO's own terms, with no copied text, type names, paths or repository names (`zaro-docs`).
- **Don't make the owner watch CI or press merge.** Open the PR, enable auto-merge
  (`gh pr merge <n> --squash --auto`) and go on with the next step (`zaro-git`).
- **Don't commit or push to `main`, and don't pass `--no-verify`.** The hooks and
  `permissions.deny` refuse both.
- **Don't build with `cmake --build` directly on Windows.** It skips the header-dependency repair
  that `tools/build.py` runs (`zaro-code`).
- **Don't run `zaro test --update` to make a failing test pass.** Update a golden hash only for an
  intended behaviour change, and say so in the commit (`zaro-scene`).
