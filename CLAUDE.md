# ZARO — agent and contributor guide

ZARO (**Z**ero-royalty, **A**I-native, **R**apid, **O**pen) is a royalty-free game engine built
to be driven by AI agents: text assets, a headless CLI, structured errors. It targets very large
entity counts (tens of thousands of enemies) with compute-shader-heavy rendering.
Every architectural decision and its reasoning is in [docs/DECISIONS.md](docs/DECISIONS.md).

## Commands

| Task | Command |
|---|---|
| Configure + build (debug) | `python tools/build.py` |
| Build + run all tests | `python tools/build.py debug --test` |
| Tests without a GPU (as CI runs them) | `python tools/build.py debug --test --no-gpu-tests` |
| Engine compile-time budget | `python tools/build.py debug --engine-budget=60` (prints `engine_build_seconds`) |
| Linux build in Docker (from Windows too) | `sh tools/ci/linux-docker.sh` |
| Clean rebuild | `python tools/build.py debug --clean` |
| Release / Tracy builds | `python tools/build.py release`, `python tools/build.py profile` |
| Lint the whole tree | `python tools/lint.py` (rules: `--list-rules`) |
| Run a scene in a window | `build/debug/zaro run examples/crowd/scene.toml` (WASD or arrows, Esc quits) |
| Headless benchmark | `build/release/zaro run examples/crowd/scene.toml --headless --perf perf.json` |
| Headless screenshot | `build/debug/zaro run --headless --frames 60 --screenshot shot.png` |
| Simulation only, no GPU | `build/debug/zaro run examples/crowd/scene.toml --sim-only --frames 300` |
| List components (fields, types, defaults, ranges) as JSON | `build/debug/zaro schema` |
| Validate a scene without running it | `build/debug/zaro check examples/crowd/scene.toml [--json]` |
| Run scene tests (no GPU) | `build/debug/zaro test examples/crowd/tests [--json]` |
| Accept a changed simulation as the new golden | `build/debug/zaro test <paths> --update` |
| Render a scene to PNG | `build/debug/zaro screenshot examples/crowd/scene.toml out.png [--frames N]` |
| All `run` options | `build/debug/zaro help` |
| Enable the pre-commit hook (once per clone) | `git config core.hooksPath tools/githooks` |

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
examples/       example scenes and prefabs (TOML)
tests/          test executables and tests/data/; c_abi_check.c compiles public headers as C
tools/          build.py, lint.py, githooks/
cmake/          ZaroCompilerFlags.cmake (zaro_target_defaults), ZaroDeps.cmake (pinned deps)
docs/           DECISIONS.md, ROADMAP.md (phases sessions pick work from), guide.md (walkthrough)
```

## Modules

Each directory under `src/` is one module and one static library. It may include only the
modules listed for it (lint rule Z007, `MODULE_DEPS` in tools/lint.py). Link dependencies in
CMakeLists.txt follow the same graph.

| Module | Role | May include |
|---|---|---|
| `core` | math, rng, hashing, jobs, logging, diagnostics, reflection, PNG; no platform or GPU | core |
| `platform` | windows, input, time, files; the only module that includes SDL | core |
| `rhi` | GPU interface `rhi.h`; `rhi_wgpu.cpp` is the only file that includes webgpu.h | core, platform |
| `sim` | gameplay state (the model), deterministic, headless-testable | core |
| `scene` | loads TOML scenes and prefabs into registered components; the only module that includes tomlc17 | core |
| `script` | sandboxed Luau scripts acting on a scene through reflection; the only module that includes Luau | core, scene |
| `render` | draws plain data (the view); never sees sim types | core, rhi |
| `app` | owns the frame loop and wires sim to render | all of the above |
| `cli` | the `zaro` command | core, app |

Third-party headers belong to one module only: `SDL3/` → platform, `webgpu/` → rhi,
`TaskScheduler.h` → `core/jobs.cpp`, `tracy/` → `core/profile.h`, `tomlc17.h` → scene, `lua.h`/`lualib.h`/`luacode.h` → script.
To add a feature, put the state and rules in `sim`, the drawing in `render`, and the
connection in `app`. A new module needs an entry in `MODULE_DEPS` (tools/lint.py), in `SCOPES`
(tools/git_rules.py) and in this table.

## Scenes and components (D-017)

The file format is documented at the top of `src/scene/scene.h`. A scene is a `[scene]` table
plus `[[entity]]` tables. Each entity has a `name`, an optional `prefab` (path relative to the
scene) and `[entity.<Component>]` tables. Values start from the component's defaults, then the
prefab, then the entity, field by field.

To add a component:
1. Define a plain struct with default member initializers in the module that owns the behaviour
   (see `src/sim/crowd_components.h`).
2. Describe it with `ZARO_FIELD` or `ZARO_FIELD_RANGE` and `ZARO_COMPONENT` (`src/core/reflect.h`),
   with a doc string for the component and for every field.
3. Register it from `app::register_all_components` (`src/app/tools.cpp`).

Field types: `bool i32 u32 u64 f32 vec2 vec3 vec4 string` (strings up to 127 bytes).
`zaro check` reports every problem in one pass as `file:line:col: error[ZE04nn]` (ids in
`src/core/log.h`), with a "did you mean" hint for misspelt components and fields.

## Scripts (D-009)

Game scripts are Luau files. The API and the sandbox are documented at the top of
`src/script/script.h`: the `on_start()` and `on_tick(tick)` hooks, and `zaro.get`, `zaro.set`,
`zaro.has`, `zaro.tick` and `zaro.log`. `zaro.set` goes through reflection, so a script can change
any registered component field with the same type and range checks as a scene file.

A scene names its script with `script = "file.luau"` in `[scene]`. `zaro run <scene>` calls
`on_start()` once and `on_tick(tick)` before every simulation tick (tick 1 is the first). Values
the script changes are read back the same tick. Today these are `CrowdSpawner.speed` and
`PlayerController.speed`; count, arena and seed are fixed at start. `zaro check` also compiles
the script and runs its top level.

A failing script ends a headless run with exit code 1. In a window it is paused and reloads when
the file is saved.

- There is no `os`, `io`, `loadstring` or `require`. The standard library and `zaro` are
  read-only, and `math.random` is seeded from the scene. Scripts are deterministic.
- Errors carry file and line: ZE0500 for compile errors, ZE0501 for runtime errors including
  API misuse (with "did you mean" hints), and ZE0502 for a call over its time budget.
- The Luau VM is built with `LUA_USE_LONGJMP=1`, so script errors never cross ZARO code as C++
  exceptions. A `zaro.*` binding raises with `luaL_error`, which does not return. Bindings must
  therefore hold no objects with destructors.

## Scene tests

`zaro test` runs `*.test.toml` files (format at the top of `src/scene/test_spec.h`). It looks in the
given files or directories, or in `tests/` when none are given. Each test runs its scene for N
ticks without a GPU, with the scripted player and the scene's script. It then checks the
`[[expect]]` values (`equals`, `min`, `max`) and the optional golden `state_hash`.

- Failures are `file:line` diagnostics: ZE0600 invalid test file, ZE0601 expectation not met,
  ZE0602 state hash differs. A script error fails the test as well.
- `--update` writes the current hash into each file. Use it only when a behaviour change is
  intended, and say so in the commit.
- CI runs the tests on Windows, Linux and macOS (ARM). The same golden hash must hold on all
  three. A ZE0602 on only one OS means float results diverged on that platform (D-005).

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

Breaking a rule fails something. Each rule is listed with whatever catches it.

| Rule | Enforced by |
|---|---|
| No exceptions, no RTTI | `/EHs-c- /GR-` or `-fno-exceptions -fno-rtti` plus `/WX` or `-Werror` in `zaro_target_defaults`; lint Z003 |
| Warnings are errors on ZARO targets | `/W4 /WX`, `-Wall -Wextra -Werror` |
| Public headers are plain C with an `extern "C"` guard | lint Z001, Z006; `zaro_c_abi_check` builds them as C |
| No heavy standard headers (`<iostream>`, `<format>`, `<functional>`, `<filesystem>`, `<regex>`, …) | lint Z002 (full list: `BANNED_HEADERS` in tools/lint.py) |
| Headers stay light: at most 12 includes, `#pragma once` | lint Z004, Z005 |
| Module layering and third-party header ownership | lint Z007 |
| Simulation is deterministic across thread counts | `crowd_determinism` test |

Lint runs in three places:
1. The Claude Code `PostToolUse` hook in `.claude/settings.json`, after every Edit or Write.
   A violation exits 2 and the agent sees the diagnostic immediately.
2. The git pre-commit hook, `tools/githooks/pre-commit`.
3. The `lint` test in ctest.

A single line can opt out with `// zaro-lint: allow Z00N <reason>`. Without a reason the
opt-out is ignored.

MSVC 19.44 compiles a bare `throw` without a diagnostic even with `/EHs-c-`. It rejects only
`try`/`catch` (C4530) and `dynamic_cast` (C4541) under `/WX`. On MSVC, lint Z003 is the only
check that catches `throw`.

## C++ subset

The engine is C++20, but only a restricted subset of it, chosen for compile speed (D-001).

- Use instead of a banned header:
  - `<iostream>` / `<format>` → `<stdio.h>` (`printf`, `snprintf`)
  - `<functional>` → a function pointer plus `void* user` (see `jobs::RangeFn`)
  - `<variant>` / `<any>` → a tagged union
  - `<filesystem>` → the platform layer (`platform::file_mtime`, `platform::base_path`)
- Report errors by return value, never by exception. Log them with `log_error(ErrorId, ...)`
  (`src/core/log.h`), which prints `zaro: error[ZEnnnn]: ...`. Ids are stable and never
  renumbered.
- Templates only for small containers and math. No template metaprogramming in headers.
- The plugin boundary (`include/zaro/`) is C: POD structs, function-pointer tables, a
  `struct_size` field first for forward compatibility (D-011).

## Simulation and parallelism

- Gameplay runs at a fixed 60 Hz (`sim::kTickSeconds`). Rendering interpolates between ticks.
- `jobs::parallel_for` bodies write only elements inside their `[begin, end)` range and read
  only data that no iteration writes. Then results are identical for any thread count. Use
  double buffers (`nvx`/`nvz` in `src/sim/crowd.cpp`) when a step reads neighbours.
- Per-entity randomness inside parallel loops comes from `hash_u32` on entity index, tick and
  seed, never from a shared RNG.

## Headless runs and perf.json

`zaro run --headless` runs one simulation tick per frame with a scripted player, renders
offscreen, and keeps at most two frames in flight. `--perf PATH` writes:

| Field | Meaning |
|---|---|
| `fps`, `frame_ms` (`avg`, `p50`, `p99`, `max`) | wall time per frame over the measured frames (after `warmup_frames`) |
| `cpu_ms.sim`, `cpu_ms.render_prep` | average CPU time of the simulation tick and of instance upload + command recording |
| `gpu_ms` | average GPU time per timed pass (`cull`, `main`) and `total`; `null` without timestamp support |
| `draw_calls`, `entities`, `enemies`, `ticks`, `hits` | per-frame draw calls and simulation counters |
| `state_hash` | simulation state after the run; equal for equal seed, tick count and build |
| `adapter`, `backend`, `threads`, `resolution`, `mode` | run environment |

The run also prints `state_hash=<hex> ticks=<n>` on stdout.

## CI

`.github/workflows/ci.yml` runs on every pull request and on pushes to `main`:

1. `rules` (Ubuntu): `tools/lint.py`, then the PR title and every PR commit through
   `tools/git_rules.py` (format and DCO sign-off).
2. `build` on Windows, Linux and macOS: `tools/build.py release --test --no-gpu-tests
   --engine-budget=180`, then a simulation-only benchmark (`zaro run --sim-only`). Its
   numbers go to the job summary and a `perf-<os>` artifact.

Hosted runners have no GPU, so `gpu`-labelled tests and GPU timings run only locally. Linux
system packages are listed in `tools/ci/install-linux-deps.sh`, which CI and
`tools/ci/linux-docker.sh` share. Actions are pinned by commit SHA.

## Shaders

Shaders are Slang (`shaders/*.slang`). `zaro_add_shaders` in `cmake/ZaroDeps.cmake` compiles
them with `slangc -target wgsl -warnings-as-errors all` into `<bin>/shaders/`. A Slang error
fails the build with the file and line. `shaders/frame.slang` must stay byte-compatible with
`FrameConstants` in `src/render/crowd_renderer.cpp`, which a `static_assert` on its size checks.

Hot reload: a windowed `zaro run` polls the `.wgsl` files twice a second. Rebuild with
`python tools/build.py` while it runs, and the pipelines are recreated (`zaro: shaders reloaded`).

## Troubleshooting

- **Header edits did not rebuild a file** (Windows, Korean-localized MSVC). In parallel clean
  builds, Ninja sometimes does not recognize MSVC's localized `/showIncludes` prefix and
  records `#deps 0` for an object (`ninja -C build/<preset> -t deps`). `tools/build.py`
  deletes such ZARO objects after each build and rebuilds them alone, which records the
  dependencies. Running `cmake --build` directly skips that repair. Third-party objects under
  `_deps/` are not repaired; they change only with a dependency bump, which reconfigures.
  English MSVC output does not need the repair.

## Branches, commits and pull requests (D-015)

- `main` always builds and passes tests. Nothing is committed or pushed to it directly.
- Work on a short-lived branch named `type/short-name`, e.g. `feat/flow-field-lod`,
  `fix/rhi-resolve-align`. Types: `feat fix perf refactor docs test build ci chore revert`.
- Commit messages: `type(scope): subject`, header at most 72 characters, no trailing period,
  plus a DCO sign-off (`git commit -s`). Scope is optional; when given it is a module
  (`core platform rhi sim scene render app cli`) or an area (`abi shaders tools cmake deps docs ci repo`).
  Example: `fix(rhi): align query resolve offset to 256 bytes`.
- One pull request per logical change. Changes are merged by **squash**; the PR title becomes the
  commit on `main`, so it follows the same `type(scope): subject` format. Reverting a PR is
  one `git revert`.
- Run `python tools/build.py debug --test` before pushing.

CI (`.github/workflows/ci.yml`) checks the PR title and every PR commit with
`tools/git_rules.py pr-title` and `commits`.
Enforced by `tools/git_rules.py` (rules G001–G006, `--list-rules`) through the git hooks in
`tools/githooks/` (`pre-commit`: branch, `commit-msg`: message, `pre-push`: no push to `main`),
and by `permissions.deny` in `.claude/settings.json` (pushes to `main`, `--no-verify`). The
GitHub repository allows squash merges only, uses the PR title as the commit title, and deletes
merged branches. The `main` ruleset (`tools/github/ruleset-main.json`, applied on GitHub) blocks
deletion, force pushes and direct pushes. It also requires the four CI checks (`lint and
repository rules` and `build and test` on each OS) before a squash merge. To land a PR once CI
passes, enable auto-merge (`gh pr merge <n> --squash --auto`); nobody has to wait for the checks.
Editing the JSON file does not change GitHub. Re-apply it with `gh api -X PUT
repos/Hoyeom/zaro/rulesets/<id> --input tools/github/ruleset-main.json`; the ruleset id is in
`gh api repos/Hoyeom/zaro/rulesets`.
