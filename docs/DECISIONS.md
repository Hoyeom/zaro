# Architecture decisions

Each entry: the decision, why, what was rejected, and when to revisit it. Entries are never
deleted. To reverse a decision, add a new entry that supersedes the old one and link the two.

Status of all entries below: **accepted, 2026-11-01**.

---

## D-001 Language: restricted C++20, C ABI at the plugin boundary

- **Decision**: The engine is C++20, limited to the subset in the zaro-code skill ("C++ subset").
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

- **Current state (M2-2)**: Luau 0.743 (VM and compiler only) runs scripts through the
  `script` module (zaro-scene skill, "Scripts"). The type checker (`luau-analyze`) is not integrated
  yet. Script errors carry file and line from the VM and compiler.

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
  use `type(scope): subject`, where scope is an engine module or a repository area (zaro-git skill).
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
  title, branches deleted after merge, auto-merge allowed). The ruleset in
  `tools/github/ruleset-main.json` has been active on GitHub since the repository went public.
  It forbids deleting `main`, force pushes and direct pushes, allows only squash merges, and
  requires the four CI checks, which include the PR-title and commit checks (D-013).

## D-016 Release and business model: open engine, private revenue, launch with a game

- **Decision**: The engine repository is public under MIT OR Apache-2.0 but not announced. The
  official launch comes together with a small commercial game built with ZARO. Income comes
  from outside this repository:
  - games made with ZARO
  - console porting layers, which are private and under NDA anyway (D-002)
  - optional paid plugins and tools in separate repositories (D-011)
  There are never runtime royalties; the name promises that. Until the launch, no outside code
  contributions are accepted, so the maintainer remains the only copyright holder. Whether to
  require a CLA is decided when contributions open.
- **Why**:
  - Nothing that earns money lives in this repository, so publishing it early costs nothing.
  - A public repository gets free CI on all three OSes and server-side protection of `main`.
  - A first impression happens once; it is saved for a launch with a playable game.
  - Sole authorship keeps the option of relicensing future versions open without a CLA.
- **Consequences**:
  - Code published here stays usable forever under MIT/Apache-2.0.
  - Anything meant to stay private is never committed here. That covers the game, console
    layers and paid plugins; each lives in its own private repository.
- **Launch criteria**: M2 (TOML scenes and prefabs, Luau scripts, `zaro screenshot` and `zaro
  test`, docs and examples), then M3 (a survivors-like built with ZARO, with a tutorial), then
  the announcement. *Superseded by D-018: M3 is replaced by the multi-platform phases in
  docs/ROADMAP.md, and the announcement waits for the flagship slice (P5).*
- **Name check (2026-11-01)**: USPTO, KIPRIS and the WIPO Global Brand Database were searched
  for "ZARO". No live mark covers game engines or developer software. The only live software
  filings are a Thai class 9 application (goods not checked) and an abandoned ZaroVerse US class
  9 filing. A software company named Zaro (zaro.ai, an AI workspace) exists without a found
  trademark. Filing ZARO in Korea for classes 9 and 42 is advised once the project becomes
  known. This was not legal advice.

## D-017 Scenes: TOML via tomlc17, reflected plain-struct components

- **Decision**: Scenes and prefabs are TOML files parsed by tomlc17 (MIT, one C file).
  Components are plain structs with default member initializers. Each one is described once by
  `ZARO_FIELD` / `ZARO_COMPONENT` macros (name, type, offset, range, doc) and registered at
  startup. The scene loader, `zaro check` and `zaro schema` work only from those descriptions.
  Prefabs are referenced by relative path, do not nest, and are overridden field by field.
- **Why**: tomlc17 compiles in about a second and keeps the line and column of every value, so
  each error can point at `file:line:col`. Plain structs keep components cheap to copy and easy
  to read in a debugger. Macros instead of templates follow D-001. Reporting every error in one
  pass, with "did you mean" hints, lets an agent fix a scene in one edit.
- **Rejected**:
  - toml++, which is header-only, slow to compile and uses exceptions by default
  - code generation from a schema file, which adds a build step
  - JSON scenes, which allow no comments and are noisier to diff
- **Not yet**: there are no entity hierarchies or component references between entities, and
  scenes do not drive `zaro run` yet (M2-3).

## D-018 Own games first, on PC and mobile, with the owner's backend through a C++ plugin

- **Decision**: ZARO's first users are its owner's own games, and those ship on PC and mobile.
  The next milestones are therefore the multi-platform foundations rather than a PC-only action
  game. They are mobile builds, a UI system, 2D sprites, and live services. The plan is in
  docs/ROADMAP.md (P1-P5).
  - **Live services**: the owner already runs a backend platform. Its server API is
    engine-neutral, with HTTP and JSON contracts plus client generators, so ZARO does not build
    its own. A private plugin (D-011) carries that platform's client.
  - **Client implementation**: the client is reimplemented in C++. Models and routes are
    generated from the API contracts (a C++ target beside the existing C# one), and the
    client-side rules are ported by hand. Each port is checked against the backend's golden
    vectors so the C++ and C# clients cannot drift apart.
  - **Run verification**: server-side verification of a ZARO game's runs replays its command log
    with the headless `zaro` runtime and compares state hashes (D-005).
  - **Flagship**: a slice of the owner's mobile idle game. The announcement waits for it.
- **Why**:
  - On mobile, builds, UI, 2D and in-app purchases are entry requirements. Without them ZARO
    cannot carry the owner's games.
  - They are not the reason to choose ZARO, though: other engines have them all. The reason is the
    agent workflow from M2 (text assets, CLI, `file:line` errors, deterministic tests,
    screenshots), and that applies to mobile games as much as to PC ones.
  - Reusing the existing backend avoids rebuilding servers, operations tools and live-ops.
- **Rejected**:
  - Embedding .NET (NativeAOT) to reuse the C# client as is. It relies on reflection-heavy
    libraries that do not fit NativeAOT well, mobile NativeAOT is less mature, and it adds a
    runtime with its own GC and binary size, against D-001's goal of a light engine.
  - PC first with the survivors-like spin-off. It does not serve the owner's main line of games,
    and it would delay the mobile foundations the flagship needs. It stays in "Later".
- **Consequences**:
  - Two client implementations, C# and C++, kept honest by contract generation and shared
    vectors.
  - The plugin ABI (D-011) gets built in P4b, earlier than planned and for the backend first,
    not for Steamworks.

## D-019 Fonts: stb_truetype into a dynamic glyph atlas, Noto Sans KR bundled

- **Decision**: The `ui` module rasterizes glyphs with stb_truetype (public domain or MIT, one
  header) into a dynamic single-channel atlas, keyed by glyph and pixel size. Glyphs are added on
  first use, so the 11,172 Hangul syllables never have to be baked up front. When the atlas is
  full it is cleared and refilled with the glyphs the next frame uses. The default font is Noto
  Sans KR (Regular and Bold, unmodified OTF), which covers Hangul, Latin and common symbols. It is
  downloaded at configure time, pinned by commit and SHA-256 like the other dependencies, and
  copied next to the executable. Games can name their own fonts.
- **Why**:
  - Compile speed (D-001): stb_truetype compiles in well under a second; FreeType is dozens of
    files and an attribution clause in every product's documentation.
  - Mobile screens are high-DPI, where hinting matters least. Hangul syllables and Latin need no
    shaping: precomposed syllables map one codepoint to one glyph, and kerning comes from the
    font's `kern` and GPOS tables.
  - stb_truetype reads TrueType and CFF (OTF) outlines, including CID-keyed CJK fonts.
- **Font license**: Noto Sans KR is under the SIL Open Font License 1.1. OFL allows bundling and
  embedding the font with software under any license, including commercial games; it forbids
  selling the font on its own and reserves its font names for modified versions. ZARO ships the
  files unmodified, so D-012 is met. A game that subsets the font to save space must rename it
  (the Reserved Font Name rule) or use a font without reserved names.
- **Rejected**:
  - FreeType: better small-size rendering through hinting, variable fonts and an SDF renderer,
    but far more to build and an attribution duty. It stays the upgrade path; font code is
    confined to one file of the `ui` module so the swap stays local.
  - Signed-distance-field glyphs: one entry serves every size and makes outlines cheap, but
    complex Hangul glyphs blur at small sizes. Outlines and shadows can be drawn as extra glyph
    passes later.
  - Prebaked bitmap fonts: they cannot cover Korean at several sizes in a reasonable atlas.
- **Revisit when**: a target needs scripts that require shaping (Arabic, Indic, Thai), or text
  below about 12 px on low-DPI screens looks too soft.

## D-020 UI screens: TOML files of nodes with reflected widget components

- **Decision**: Screens are TOML files, in the same form as scenes (D-017). A `[screen]` table
  names the screen. Each `[[node]]` has a `name`, an optional `parent` (another node's name, so
  the tree is a flat list) and `[node.<Widget>]` tables. Widgets (`Layout`, `Panel`, `Label`,
  `Image`, `Button`, `ScrollList`, `Modal`, …) are plain structs described with the reflection
  macros and registered like scene components, but marked as UI components so they cannot be
  used in scenes and vice versa. The scene loader reads both kinds of file, so `zaro check` gives
  screens the same `file:line:col` errors, range checks and "did you mean" hints, and
  `zaro schema` lists every widget. Text that starts with `@` is a key into a per-language string
  table.
- **Why**:
  - One format and one loader: an agent that can write a scene can write a screen, and every
    diagnostic and tool already works.
  - A flat node list with `parent` keys diffs line by line, and nesting depth costs no
    indentation. Deeply nested TOML tables would be hard to read and to edit.
  - Reflected widgets give `zaro.set` from Luau and scene-test `[[expect]]` checks on widget
    fields for free.
- **Rejected**:
  - A dedicated markup (XML-like or KDL-like): trees read more naturally, but it needs a new
    parser, new error positions and new hints, and agents would learn a second format.
  - Nested `[[node.children]]` tables: the tree shows in the file, but each level lengthens every
    table header.
  - Immediate-mode UI written in Luau: flexible, but screens could no longer be checked without
    running code, and layouts would not be text assets.

## M2 result (2026-11-01)

M2 (agent workflow, D-016) is done. An agent can now build a scene end to end through the CLI:
- `zaro schema` tells it what components exist.
- `zaro check` reports every problem in a scene or script with `file:line` and a "did you mean"
  hint.
- `zaro run` plays the scene with its Luau script.
- `zaro test` checks values and a golden state hash without a GPU.
- `zaro screenshot` shows the result.

The walkthrough is `docs/guide.md`, with `examples/hello` and `examples/crowd`. Every command
has text and `--json` output where it reports problems. The example tests' golden hashes were
verified locally on Windows (MSVC) and Linux (GCC). CI runs them on macOS (Apple Clang, ARM)
as well, and a merge requires them to pass there.

Still open from the M2 list: the Luau type checker (D-009) and entity hierarchies (D-017).

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
