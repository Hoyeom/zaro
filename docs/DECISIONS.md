# Architecture decisions

Each entry: the decision, why, what was rejected, and when to revisit it. Entries are never
deleted. To reverse a decision, add a new entry that supersedes the old one and link the two.

Status of all entries below: **accepted, 2026-11-01**.

---

## D-001 Language: restricted C++20, C ABI at the plugin boundary

- **Decision**: The engine is C++20, limited to the subset in CLAUDE.md ("C++ subset").
  Exceptions and RTTI are off. Public headers stay STL-free. Builds use unity builds and skip
  the C++20 module scan. Everything under `include/zaro/` is plain C.
- **Why**: Compile speed is the top priority, but C makes vector math and containers verbose,
  and most useful dependencies (Slang, Luau, Jolt, Tracy, Basis) are C++. The subset keeps
  C++ productivity at close to C compile times.
- **Enforcement**: Rules written only in CLAUDE.md were not followed reliably. Each rule is
  therefore enforced by a compiler flag or a lint rule, run by the Claude Code hook,
  pre-commit and ctest (see CLAUDE.md "Rules that are enforced"). CI adds a clean-build time
  budget (D-013).
- **Rejected**: pure C17, which makes math verbose and every C++ dependency needs a wrapper.
  A C core with C++ tools, which means two rule sets and a constantly moving boundary.

## D-002 Platform and graphics: SDL3 + own thin RHI, first backend wgpu-native

- **Decision**: SDL3 (zlib) provides windows, input, gamepad, touch, IME, and audio later.
  Rendering goes through an engine-owned RHI interface. The first backend is wgpu-native
  (MIT/Apache-2.0), driven through the standard `webgpu.h`. Native features used: compute,
  indirect draw, multi-draw-indirect(-count), timestamp queries.
- **Why**: Compute is mandatory. GPU timestamps are required for `perf.json`. The same API
  covers the web target (2nd phase). Validation errors are readable, which helps agents.
  Owning the RHI interface means console backends can live in private repositories.
- **Rejected**:
  - SDL3 GPU: no timestamp queries, no indirect-count, no official WebGPU backend.
  - bgfx: own shader language and build system, incomplete WebGPU backend.
  - Raw Vulkan RHI: most code to write, MoltenVK on macOS, a separate web backend.
- **Consequences**: no bindless and no indirect-count on the web, so designs must have a path
  that does not need them. `webgpu.h` keeps a later swap to Dawn possible.
- **Revisit when**: a needed feature is missing from wgpu-native on desktop.

## D-003 Shaders: Slang

- **Decision**: Shaders are written in Slang. Slang compiles them to WGSL for the wgpu
  backend, and to SPIR-V, MSL or HLSL for future native backends. In development the engine
  compiles at runtime for hot reload. Shipped builds use precompiled output.
- **Why**: HLSL-like syntax that LLMs write well, a module system and generics, one source for
  every target.
- **Rejected**: hand-written WGSL (no modules, weaker LLM familiarity). HLSL + DXC +
  SPIRV-Cross (three-stage toolchain, still needs a WGSL step).
- **Consequences**: the Slang compiler library is large. It ships only in tools and dev builds.
- **Current state (M1)**: shaders are compiled at build time by the `slangc` executable.
  Windowed runs reload changed `.wgsl` files, so "hot reload" means rebuilding while the app
  runs. Runtime compilation through the Slang library is not implemented yet.

## D-004 Build: CMake + Ninja

- **Decision**: CMake (≥ 3.28) with Ninja, presets in `CMakePresets.json`, and
  `tools/build.py` as the one-command entry point. Dependencies are pinned by version or hash.
- **Why**: Android Gradle, Xcode and IDEs all integrate with CMake. Both tools ship with the
  VS Build Tools already installed.
- **Rejected**: Meson (weaker mobile-project export), a custom build script (all IDE and mobile
  integration would have to be built by hand).

## D-005 Simulation: fixed 60 Hz tick, same-build/same-platform determinism

- **Decision**: Gameplay runs in a fixed 60 Hz tick, and rendering interpolates between ticks.
  Gameplay state is authoritative on the CPU. The GPU only renders and produces
  non-authoritative effects. RNGs are seeded, inputs are recorded, and a state hash is taken
  every tick. Float contraction is off (`/fp:precise`, `-ffp-contract=off`).
- **Why**: Replays and golden tests have to be stable. Bit-exact results across
  architectures would need fixed-point or soft-float math everywhere.
- **Guarantee**: identical results for the same binary on the same platform. In practice x64
  machines match. ARM vs x64 is not guaranteed.
- **Rejected**: cross-platform bit-exact (cost), GPU-authoritative simulation (breaks replays
  and tests).

## D-006 Multiplayer: not now, keep the door open

- **Decision**: No networking code. The simulation stays a pure `state + inputs → next state`
  step, and snapshot serialization exists from the start. That is enough to add lockstep or
  rollback later.
- **Revisit when**: co-op becomes a product goal. Expect stricter rules on math and ordering then.

## D-007 Threading: enkiTS parallel_for from M1

- **Decision**: Systems are written as data-parallel loops over SoA arrays and split with
  enkiTS (zlib). Ranges never overlap and reductions run in a fixed order, so determinism
  holds. `--threads 1` forces single-threaded runs for debugging. A separate render thread
  comes later.
- **Rejected**: single-threaded first (the structure would need rework), a custom fiber
  scheduler (too much work this early).

## D-008 Assets: glTF 2.0 + KTX2/Basis Universal, cooked cache

- **Decision**: The source formats are glTF 2.0 (cgltf, MIT) and KTX2 with Basis Universal
  (Apache-2.0). PNG is read for convenience and converted at import. Runtime binaries are a
  content-hashed derived cache, gitignored, so git holds only sources and TOML.
- **Rejected**: loading glTF/KTX2 directly at runtime (slow loads, no per-platform tuning).

## D-009 Scripting: Luau

- **Decision**: Game logic scripts are Luau (MIT), run sandboxed, with hot reload.
  `luau-analyze` type errors are reported with file and line.
- **Why**: Luau is sandboxed by design and has gradual typing. Its fast interpreter needs no
  JIT, so it runs on iOS. LLMs know Lua/Luau well.
- **Rejected**: TypeScript on QuickJS-ng (needs a TS→JS step and source-map remapping, and is
  slower). Plain Lua 5.4 (no type checking).
- **Rule**: scripts orchestrate. Per-entity work for tens of thousands of entities stays in
  C++ systems.

## D-010 Profiling: Tracy as an opt-in build + built-in perf counters

- **Decision**: Tracy (BSD-3) is compiled in only with `ZARO_TRACY=ON` (the `profile` preset).
  Zone macros go into the code from the start and cost nothing when Tracy is off. Separately,
  the engine's own CPU scopes and GPU timestamps produce `perf.json` for headless runs and CI.
- **Current state (M1)**: the `profile` preset fetches Tracy v0.14.1 (on-demand mode) and
  defines `ZARO_TRACY`. `ZARO_ZONE` / `ZARO_FRAME_MARK` in `src/core/profile.h` mark the
  simulation steps, jobs and RHI frame boundaries.

## D-011 Native plugin ABI: C function-pointer tables + ABI major version

- **Decision**: A plugin exports one entry point. It exchanges structs of function pointers
  with the host, and each struct starts with `struct_size`. Additions go only at the end, and
  `ZARO_ABI_MAJOR` changes only on a break. Desktop plugins load dynamically. iOS and consoles
  register the same code statically. `zaro-plugin.toml` declares the plugin's version, ABI,
  platforms, native libraries, Gradle/Xcode fragments and script bindings. `zaro plugin add
  <name>` installs a plugin.
- **Game-facing API**: scripts call one interface per service, e.g. `iap.purchase("gem_100")`.
  The installed plugin routes the call to the platform implementation. On mobile, plugins reach
  native SDKs through platform bridges: JNI on Android, Objective-C++ on iOS.
- **Distribution**: proprietary SDKs never enter the engine repository. Each one lives in its own
  plugin repository (`zaro-steam`, `zaro-firebase`, `zaro-iap`, …). The manifest declares
  everything `zaro plugin add` needs, so an agent can install and configure a plugin end to end.
- **Validation**: Steamworks (`zaro-steam`) is the first real plugin, in the desktop phase.
  Firebase and IAP follow in the mobile phase. The ABI and the Gradle/Xcode export structure are
  designed now, so they do not have to change then.
- **Rejected**: name-based symbol lookup (many string keys, awkward static linking).

## D-012 License: MIT OR Apache-2.0; permissive dependencies only

- **Decision**: The engine is dual-licensed MIT OR Apache-2.0, at the user's choice.
  Dependencies must be MIT, BSD, zlib, Apache-2.0 or similar permissive licenses.
  GPL/LGPL/AGPL are not allowed. Contributions use the DCO (CONTRIBUTING.md).
- **Why**: Apache-2.0's patent grant protects users, MIT covers GPLv2 compatibility, and Slang,
  Basis Universal and wgpu can be used as they are.
- **Planned enforcement**: CI checks the dependency license list (with D-013).

## D-013 CI (planned): 3-OS build, lint, build-time budget, perf regression

- **Decision**: GitHub Actions builds on Windows, Linux and macOS, then runs lint and ctest. A
  clean-build time budget fails the job when it is exceeded. Perf regression is tracked from
  `perf.json`. CI runners have no GPU, so GPU-time regressions are tracked on a self-hosted
  runner. Hosted runners track CPU simulation time.
- **Current state**: `.github/workflows/ci.yml` runs lint and repository rules, and builds
  and tests on Windows, Linux and macOS (GPU tests excluded). `--engine-budget=180` fails the
  job when rebuilding ZARO's own objects takes longer than 180 s. A simulation-only benchmark
  is uploaded as `perf-<os>.json` and shown in the job summary.
- **Not done yet**:
  - automatic perf-regression gating; shared runners vary too much for a fixed threshold
  - GPU timing in CI, which needs a self-hosted GPU runner
  - the dependency-license check from D-012

## D-014 Module layering: sim (model) / render (view) / app (binding)

- **Decision**: Engine code is split into modules under `src/`: core, platform, rhi, sim,
  render, app and cli. Each module is its own static library with a fixed list of modules it
  may include (CLAUDE.md "Modules"). `sim` holds gameplay state and never sees windows or the
  GPU. `render` draws plain arrays and never sees sim types. `app` connects them and owns the
  frame loop. Third-party headers are owned by a single module.
- **Why**: Agents and contributors can change gameplay, rendering or a platform backend
  without touching the others. `sim` stays deterministic and testable without a GPU, and
  console or mobile ports replace only `platform` and `rhi`. This is the model/view
  separation that MVVM gives UI code. MVVM itself is reserved for the editor and game UI,
  which do not exist yet.
- **Enforcement**: lint rule Z007 checks every `#include`. Link dependencies in
  CMakeLists.txt follow the same graph.

## D-015 Branches, commits and merges: trunk-based, Conventional Commits, squash

- **Decision**: `main` is the only long-lived branch and always passes the build and tests.
  Work happens on short-lived `type/short-name` branches and lands through pull requests that
  are squash-merged. The PR title becomes the commit on `main`. Commit messages and PR titles
  use `type(scope): subject`, where scope is an engine module or a repository area (CLAUDE.md).
  Every commit carries a DCO sign-off. Releases are `v0.x.y` tags on `main`.
- **Why**: Each PR is one commit on `main`, so an agent's change can be reverted with one
  `git revert`, and `git log --grep '(sim)'` finds a module's history. The format makes a
  generated changelog and version bumps possible later. Squash keeps the sign-offs because
  the squash message is the concatenated commit messages (`COMMIT_MESSAGES`).
- **Rejected**: Git Flow (develop/release branches cost more than they give at this stage and
  make agents guess the target branch), rebase merge (every intermediate commit would have to
  be polished), merge commits (non-linear history complicates bisect and reverts).
- **Enforcement**: `tools/git_rules.py` via git hooks, `permissions.deny` in
  `.claude/settings.json`, and GitHub repository settings (squash only, PR title as commit
  title, branches deleted after merge). A ruleset protecting `main` is not available for
  private repositories on the free plan. It is kept in `tools/github/ruleset-main.json` and
  applied when the repository becomes public. CI will check PR titles (D-013).

## M1 result (2026-11-01)

The crowd demo (`zaro run`) has 50,000 enemies following a flow field around obstacles toward
the player, with spatial-hash separation, GPU frustum culling and one indirect draw.
Release build, RTX 5070, Vulkan, 1920x1080, `--headless --warmup 60 --frames 600`:

| Threads | fps | frame avg / p99 ms | sim ms | GPU ms | draw calls |
|---|---|---|---|---|---|
| 24 | 791 | 1.26 / 2.94 | 0.98 | 0.06 | 4 |
| 1 | 105 | 9.57 / 32.3 | 8.92 | 0.06 | 4 |

Both runs ended with the same `state_hash`. 200,000 enemies ran at 217 fps with 24 threads.
These numbers come from one machine and one run each. They are a reference point, not a
regression baseline; D-013 defines that.

---

## Deferred (design kept open, not built)

Gamepad/touch input, CJK fonts and IME, localization, audio (miniaudio candidate), game UI,
save-format versioning, physics (Jolt candidate), the MCP server (local only, access-restricted).

## Before the repository goes public

- Check trademark, company-name, domain and GitHub-organization conflicts for "Zaro".
- Enable GitHub private vulnerability reporting (SECURITY.md relies on it).
- Protect `main`: `gh api -X POST repos/<owner>/zaro/rulesets --input tools/github/ruleset-main.json`.
