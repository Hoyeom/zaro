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
- **Current state (P2 step 1)**: `src/ui/font.cpp` wraps stb_truetype 1.26; `src/ui/text.cpp`
  holds the 2048 x 2048 atlas (shelf packing, a 16,384-slot glyph cache) and line layout. Glyph
  positions snap to whole pixels. Kerning comes from the font's `kern` and GPOS pair tables.

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
- **Current state (P2 step 3)**: `scene::load_document` reads both kinds of file. String tables are
  one TOML file per language whose nested tables flatten to dotted keys; `zaro check` on a scene
  checks every `@key` against every language. A screen's `reference` size scales it to the
  target (the largest scale at which it fits).
- **Current state (P2 step 4)**: Luau binds to the UI through a view model (`ui.set`, `{name}`
  placeholders in texts) and through the same reflection as scenes (`ui.set_field`), so screens
  stay pure data and every change a script makes can be checked by a scene test.

## D-021 UI navigation: a screen stack with one policy for back, close, home and reset

- **Decision**: Screens are navigated by one policy in the `ui` module (`ui::Navigator`), so every
  game behaves the same. It follows the UI navigation rules of the owner's backend platform, so a
  game moving between that platform's existing client and ZARO keeps the same behaviour.
  - **Stack**: `[scene] ui` lists root screens (the HUD), which are always shown and never closed
    by navigation. `[scene] screens` lists screens that buttons (`action = "open:<name>"`) and
    scripts (`ui.open`) open on top. Each open is a new instance unless the caller passes a key that
    an open instance already has; reuse is the caller's choice, never a guess.
  - **Four operations**: back (the top screen, by the rules below), close (a named screen, or the
    one a `close` button is on), home (close down to the root, stopping at a forced screen), and
    reset (remove every opened screen at once, forced ones included: scene change, logout).
  - **Lifecycle**: opening (only when opened with `wait`, until `ui.ready`; a spinner shows) →
    entering (entrance transition) → open → closing (exit transition) → gone, one direction. Only
    open screens take input; a closing screen has already handed input to the one below, and an
    open modal hides input from everything below it.
  - **Three properties per screen file**: `dismissible` (back may close it), the entrance
    `transition` and whether it is `skippable`, and (through a `Modal` widget) whether it blocks the
    screens below. An open call may only tighten them (`force`, `unskippable`).
  - **Back**: first the script's `on_back(screen)` may claim the press for something inside the
    screen, and then nothing closes. Otherwise, by the top screen's state: opening and dismissible
    → cancelled; entering → a skippable transition jumps to its end, an unskippable one drops the
    press (never queued); open → a dismissible screen closes, a forced one keeps the press. With
    nothing opened, the back is reported as unhandled and the game decides (the crowd demo quits).
- **Why**:
  - A back press while a screen waits for the server is a player who wants out, and must cancel.
    A press during a 0.15 s entrance is almost always a leftover double tap, and must not close
    what just opened.
  - A queued press would close a screen at a moment the player did not choose.
  - Separate close and reset keep a player's close animated and a scene change instant.
  - Keeping the policy out of the renderer makes every rule a plain unit test (`test_ui_nav`) and
    a scene test (`[[back]]`, `state = "..."`).
- **Rejected**:
  - Hiding and showing nodes of always-loaded screens (step 3 and 4 of P2 did this): no lifecycle,
    no transitions, and every popup's state survives closing.
  - Letting each game script its own back handling: the same screens would behave differently
    from game to game, which is what this decision exists to prevent.
- **Not yet**: per-screen telemetry of opens and closes, routing over several hops (deep links,
  tutorials), and pad button prompts per controller family.

## D-022 Android: pinned NDK toolchain, own CMake build, SDLActivity, Gradle only packages

- **Decision**: ZARO builds for Android with its own CMake build and a pinned toolchain; Gradle
  only packages the result.
  - **Toolchain**, pinned in `tools/android.py` and installed by `python tools/android.py setup`
    (every download checked against a pinned hash): NDK r29 (29.0.14206865), SDK platform
    `android-36` (compile and target SDK), build-tools 36.0.0, Eclipse Temurin JDK 21, Gradle
    9.8.2 and the Android Gradle Plugin 9.5.0. The SDK goes to `ANDROID_HOME` (default
    `%LOCALAPPDATA%\Android\Sdk` or `~/Android/Sdk`), JDK and Gradle to `%LOCALAPPDATA%\zaro`
    or `~/.local/share/zaro`. CI installs the same versions with the same script.
  - **Target**: arm64-v8a only, `minSdk` 29 (Android 10), Vulkan 1.1 required in the manifest.
  - **Native build**: the `android` CMake preset cross-compiles with the NDK's toolchain file
    (`python tools/build.py android`). Slang runs on the host and the WGSL goes into the APK.
    wgpu-native is the upstream `android-aarch64` prebuilt, pinned by SHA-256 like the desktop
    ones. SDL3 is built as `libSDL3.so`, and its Java side (`SDLActivity`) comes from the same
    pinned SDL source. The C++ runtime is `c++_static`.
  - **Entry point**: `dev.zaro.ZaroActivity` extends `SDLActivity` and calls `main` in
    `libmain.so`, which is the `zaro` command line itself. Arguments come from the launch
    intent's `args` extra, or from `assets/zaro_args.txt` (`run game/<scene>`).
  - **Assets**: `zaro export android` packs the scene's whole directory, without scene tests
    (a script may name files at run time), and every file the scene names outside it (script,
    prefabs, screens with their prefabs and images, string tables) under `assets/game/`. Fonts
    go under `assets/fonts/` and shaders under `assets/shaders/`. At start the runtime copies
    every file listed in `assets/zaro_assets.txt` to the app's external files directory
    (`/sdcard/Android/data/<package>/files`) and runs there. Loaders keep reading plain files,
    and `adb pull` can fetch the perf report. stdout and stderr go to logcat (tag `zaro`).
  - **Project export**: the native build writes a runtime package (`cmake/ZaroAndroid.cmake`):
    the Gradle template from `src/platform/android/project`, SDL's Java sources, the stripped
    `.so` files and the shaders. `zaro export android <scene>` fills in that template. Then
    `tools/android.py apk` runs Gradle, and `run` and `perf` drive a device through `adb`.
- **Why**:
  - One build graph for every platform. The same compiler flags, lint and dependency pins apply,
    and an agent builds Android with the same `build.py` it already knows.
  - Unpacking to files keeps the scene, script and test loaders free of Android code. The files
    are small (a scene and its script), so copying them at start costs milliseconds.
  - SDL already owns windows and input on desktop; its Android backend brings touch, lifecycle
    and the surface with no new dependency.
- **Rejected**:
  - Gradle `externalNativeBuild`: a second build graph with the SDK's CMake (3.22 there, ZARO
    needs 3.28), and agents would have to go through Gradle to build native code.
  - The toolchain bundled with an installed Unity editor: its versions move with Unity, and the
    engine must not depend on another engine's install.
  - `GameActivity` without SDL: it needs AndroidX and Prefab, and it would add a second platform
    layer next to SDL.
  - Reading assets through `AAssetManager` in every loader: Android code in `scene`, `script`
    and the test runner, against the platform isolation rule.
- **Consequences**: building an APK takes two steps (native build, then export and Gradle).
  `tools/android.py apk` runs them in order. 32-bit ARM and x86 emulators are not supported. The
  APK is signed with the debug key; store signing belongs to each game. iOS follows the same
  pattern with Xcode later in P1.
- **Revisit when**: a game needs 32-bit devices, Play Asset Delivery for large assets, or a
  Gradle-side native build (for example for an AAR plugin).
## D-023 UI themes: design tokens in a TOML theme, imported from Claude Design kits

- **Decision**: A screen names a theme file (`theme = "kit/theme.toml"` in `[screen]`). The theme
  holds colours (one table per colour theme), lengths (spacing, radius, stroke) and type styles
  (`size`, `line_height`, `weight`). Any colour or number field of a screen or of a UI prefab it
  uses, vector elements included, may be written `"$name"`; the loader resolves it in the first
  colour theme and reports an unknown token as ZE0409 with "did you mean". `zaro import-design <kit>
  <out>` writes the theme from a Claude Design kit's `tokens.json`. It also writes a UI prefab stub
  for each of the kit's components, which the importer never overwrites. A designer filling one in
  with tokens makes the kit's component a ZARO prefab. `--check` fails when the theme no longer
  matches the kit, so CI catches a kit change that was not imported.
- **Why**:
  - Design systems are made as tokens and components. Taking the tokens as they are keeps one
    source of truth, and a re-import updates every screen at once.
  - Resolving tokens at load time keeps widgets plain data (D-020) and keeps every token error a
    `file:line` diagnostic before anything runs.
  - Kit components are web code (HTML, CSS, React). Their look cannot be translated into widgets
    reliably, so the importer names them and hands each to a person or an agent as a prefab stub,
    with the kit's one-line guideline, instead of guessing.
  - JSON is read with cJSON (MIT, one C file), confined to the importer like other third-party
    headers.
- **Conversions**: hex, `rgb()`/`rgba()` and `hsl()`/`hsla()` colours, aliases (`"{other}"`),
  `px`/`rem`/`em` lengths (16 px per rem), and line heights in px or as multiples. Box-shadows,
  wide-gamut colours (`oklch()` and the like) and font files are reported as skipped. ZARO draws
  with the fonts it loads; a kit's hosted fonts must be bundled to be used.
- **Rejected**:
  - Resolving tokens at draw time: themes could switch live, but every widget field would need a
    token or value variant, against D-020's plain structs.
  - Converting the kit's CSS into widgets: brittle, and wrong in exactly the cases a designer cares
    about.
- **Not yet**: switching colour themes (light/dark) at run time; widgets for the kit's
  box-shadows and outlined text.

## D-024 SVG art: PlutoSVG on PlutoVG, in the zaro tool's cook step only

- **Status**: accepted, 2026-11-02. **Superseded by D-028** (same day): PlutoSVG does not draw
  `clipPath` or `<style>` sheets, contrary to the "Why" below.
- **Decision**: SVG character art is rasterized into sprite atlases by PlutoSVG (MIT) drawing with
  PlutoVG (MIT), pinned at plutosvg v0.0.8 and plutovg v1.3.3 (the commit plutosvg v0.0.8 pins).
  Only the `zaro` tool's cook step links them. Games load the cooked atlases as PNG now and KTX2
  later (D-008), so no game binary contains either library. The cook reports every element it
  cannot draw (`mask`, `text`, `filter` and the like) as a ZE error with file and line instead of
  dropping it, so art that would render wrong fails the cook.
- **Why**:
  - Illustrator, Figma and Inkscape exports commonly style shapes through `<style>` classes and
    use `clipPath`, `use`/`symbol` and gradients. PlutoSVG draws all of these.
  - Both are plain C, so they build with ZARO's flags (no exceptions, no RTTI), and they are
    small: one 84 KB source file for SVG, and a vector library of about 300 KB without the stb
    copies it bundles. Both had releases in 2026.
  - Cooking at build time keeps parsing and rasterizing off the device and lets the cook pack,
    trim and pad frames once.
- **License duty**: PlutoVG's scan converter and stroker are FreeType code under the FreeType
  License (`FTL.TXT` in its source). That license is permissive (BSD-style, no copyleft), but a
  binary distribution must state in its documentation that it is based in part on the work of
  the FreeType Team. A distribution of the `zaro` tool therefore carries that statement in its
  third-party notices, which the PR adding the dependency creates. Games built with ZARO owe
  nothing for it, since they do not contain the code.
- **Rejected**:
  - nanosvg (zlib, two headers): the smallest and the cleanest license, but it skips the contents
    of `<style>` and has no `clipPath` or `use`, so common exports render wrong without any
    warning. It is also no longer maintained.
  - ThorVG (MIT, its rasterizer also FreeType-derived): about 900 KB of C++ for the SVG loader and
    software renderer, plus Lottie and more that P3 does not need.
  - resvg (Apache-2.0): the most complete SVG support, but it needs a Rust toolchain in the build.
  - lunasvg (MIT): C++ on top of the same PlutoVG, with nothing P3 needs over PlutoSVG.
  - librsvg and cairo: LGPL, excluded by D-012.
- **Not supported**: masks, text and filters (drop shadows, blurs). Artists convert text to
  outlines and bake effects into shapes before export. Revisit if the owner's art needs them.

## D-025 Google Play release builds: requirements as checked on 2026-11-02

- **Decision**: ZARO's Android release path follows Google Play's current requirements, listed below
  with their official sources and the date they were read. The next check compares against this
  list and records what changed in a new entry. ZARO's release build (`tools/android.py aab
  --release`) is an Android App Bundle that targets API 36, carries only 16 KB-aligned native
  libraries, is signed with the game's upload key, and comes with a native debug symbols file.
  Google holds the app signing key (Play App Signing); ZARO never holds or produces store-signed
  artifacts.
- **Requirements, checked 2026-11-02** (only what the linked pages say):
  - **Target API**: since 2026-08-31, new apps and app updates must target Android 16 (API 36) or
    higher; existing apps must target API 35 or higher to stay available to new users on newer
    devices. Extensions ran to 2026-11-01. No later requirement was stated.
    [developer.android.com/google/play/requirements/target-sdk](https://developer.android.com/google/play/requirements/target-sdk)
    (updated 2026-10-28)
  - **16 KB page sizes**: apps targeting API 35 or higher must support 16 KB pages on 64-bit
    devices. From 2027-02-01, updates that do not cannot be released. NDK r28 and later link
    16 KB-aligned by default. Each `.so` must have LOAD segments aligned to at least `2**14`
    (`llvm-objdump -p`), and uncompressed libraries must sit on a 16 KB zip boundary; a bundle
    requests that with `PAGE_ALIGNMENT_16K` in its bundletool config.
    [developer.android.com/guide/practices/page-sizes](https://developer.android.com/guide/practices/page-sizes)
    (updated 2026-10-16)
  - **App Bundles and size**: new apps must publish as an AAB (since August 2021). Limits on the
    compressed download: base module 500 MB, each feature module 500 MB, each asset pack 1.5 GB,
    all modules plus install-time asset packs 4 GB, on-demand and fast-follow asset packs 30 GB,
    34 GB overall.
    [developer.android.com/guide/app-bundle](https://developer.android.com/guide/app-bundle)
    (updated 2026-06-24),
    [support.google.com/googleplay/android-developer/answer/9859372](https://support.google.com/googleplay/android-developer/answer/9859372)
  - **Signing**: with Play App Signing, Google manages the app signing key and the developer keeps
    an upload key. New apps are enrolled automatically in "quantum-ready, hybrid signing with
    Google-generated keys" (RSA 4096 plus ML-DSA-65). The upload key must be an RSA key of 2048
    bits or more in a Java keystore. A lost or compromised upload key can be reset by Google.
    [support.google.com/googleplay/android-developer/answer/9842756](https://support.google.com/googleplay/android-developer/answer/9842756)
  - **Native debug symbols**: one file per app version. Its layout is one directory per ABI holding
    the unstripped `.so` files. The size limit differs between sources: 800 MB in the Play Console
    Help, 1.6 GB on developer.android.com.
    [developer.android.com/build/include-native-symbols](https://developer.android.com/build/include-native-symbols)
    (updated 2026-09-21),
    [support.google.com/googleplay/android-developer/answer/9848633](https://support.google.com/googleplay/android-developer/answer/9848633)
  - **Play Billing Library**: since 2026-08-31 (extension to 2026-11-01), new apps and updates must
    use version 8 or later. Version 8 reaches the same deadline on 2027-08-31 and version 9 on
    2028-08-31. This concerns the billing bridge in P4c.
    [developer.android.com/google/play/billing/deprecation-faq](https://developer.android.com/google/play/billing/deprecation-faq)
    (updated 2026-10-23)
  - **Play Integrity**: standard requests (low latency, replay protection by Google Play) are the
    recommended kind; classic requests remain. The default quota is 10,000 requests per day.
    Verification belongs to P4c.
    [developer.android.com/google/play/integrity/overview](https://developer.android.com/google/play/integrity/overview)
    (updated 2026-04-20)
  - **Developer verification**: Play apps had to be registered in Play Console by 2026-09-30.
    From the same date, certified devices in Brazil, Indonesia, Singapore and Thailand require
    apps from registered developers, and global rollout is planned for 2027.
    [support.google.com/googleplay/android-developer/answer/17134731](https://support.google.com/googleplay/android-developer/answer/17134731),
    [developer.android.com/developer-verification](https://developer.android.com/developer-verification)
  - **Technical quality from February 2027**: games must stay under memory thresholds (anonymous
    RSS plus swap, 90th percentile; for example 2.25 GB in the foreground on 4 GB devices and
    3.5 GB on 8 GB devices), bitmap memory thresholds, and a code optimization rule for games with
    more than 50 MB of DEX. ZARO's DEX is SDL's Java side, far below that.
    [support.google.com/googleplay/android-developer/answer/17492799](https://support.google.com/googleplay/android-developer/answer/17492799),
    [support.google.com/googleplay/android-developer/answer/18100766](https://support.google.com/googleplay/android-developer/answer/18100766)
  - **API 37 and later**: apps targeting Android 17 must declare the local network permission and
    use the location button for one-time location. ZARO uses neither.
    [support.google.com/googleplay/android-developer/answer/18100766](https://support.google.com/googleplay/android-developer/answer/18100766)
- **Why**: the requirements change every year. A dated list with links turns the next check into a
  diff, and the build checks below keep a release from silently falling behind.
- **How ZARO meets them**:
  - The Gradle template targets API 36 and builds an AAB with `bundleRelease`.
  - The native build fails if a packed `.so` has a LOAD segment aligned below 16 KB
    (`cmake/ZaroCheckElf.cmake`). `tools/android.py aab` runs `bundletool validate`, checks the
    bundle's `PAGE_ALIGNMENT_16K`, and runs `zipalign -c -P 16` on the device APKs bundletool
    builds from it.
  - The upload key comes only from the `ZARO_UPLOAD_*` environment variables; `aab --release`
    stops without them, and lint Z009 rejects keystores and literal signing passwords in this
    repository. The version code and name come from the game (`zaro export android
    --version-code N --version-name S`).
  - `native-debug-symbols.zip` holds the unstripped libraries, one directory per ABI.
  - ZARO's CI builds a debug-key bundle and a release bundle signed with a key generated for that
    run. How a game's CI keeps its real upload key is in
    [docs/android-release.md](android-release.md).
- **Revisit when**: Google announces the next target API year, before 2027-02-01 (16 KB and the
  quality thresholds), and before P4c ships billing (Billing Library 9 by 2028-08-31).

## D-026 Build extensions: what a plugin adds to an exported Android project, as text

- **Decision**: A plugin that needs more than native code on Android describes its additions in a
  build extension file. Examples are P4c's Play Billing bridge and later store or SDK plugins.
  - **The file**: TOML with a `[build_extension]` table (a name) and an `[android]` table that
    lists Gradle dependencies (`group:artifact:version`), permissions, an XML fragment for
    `<manifest>`, an XML fragment for `<application>`, and a directory of Java or Kotlin sources.
    The format is at the top of `src/scene/build_extension.h`.
  - **Where it is named**: a scene lists its extensions with `build_extensions = [...]` in `[scene]`.
  - **What checks it**: the scene loader loads every extension, so `zaro check` reports a bad
    entry as `file:line:col` (ZE0410 for a malformed dependency, permission or fragment, ZE0406
    for unknown keys, with a "did you mean" hint).
  - **What the export does**: `zaro export android` writes the dependencies into
    `app/build.gradle`, and the permissions and fragments into `AndroidManifest.xml` through
    `@ZARO_...@` placeholders in the template. It copies the sources under `app/src/main/java`, and
    refuses (ZE0800, at the extension's `sources` line) a source file that would replace one of the
    runtime's or another extension's.
- **Why**:
  - Plugins live in private repositories (D-012, D-016) and change separately from the engine;
    the template must not be edited per game.
  - A text file keeps the additions reviewable and diffable, and puts them under the same checks
    as scenes. An agent writes one the way it writes a scene.
  - Gradle's own merging (dependencies, manifest merger, source sets) does the rest. ZARO only
    places text where Gradle expects it.
- **Rejected**:
  - Arbitrary Gradle script snippets: anything could run during the build, and nothing could be
    checked before Gradle runs.
  - AAR modules in the plugin repository: heavier to build and publish, and still needing a
    manifest and dependency description. A plugin may still ship an AAR through a Maven
    dependency.
  - A separate export option per plugin (`--plugin`): the scene would no longer say what the game
    needs, and `zaro check` would not see it.
- **Consequences**: the XML fragments are checked only for placement (they must be elements and
  must not open `<manifest>` or `<application>`); malformed XML inside them fails in Gradle's
  manifest merger instead. Only Android has a section today; iOS adds its own table when P1
  reaches it.

## D-027 Build pipeline: one staged `zaro build` for every target, declared in text

- **Decision**: Games are built by one pipeline, `zaro build <build.toml>`, whatever the target.
  - **Stages**, always in this order: validate, pre, native, export, post, package, sign. Validate
    and pre run for every build. Native, export, post, package and sign belong to a target:
    Android first, with iOS and PC reusing the same stages.
  - **The game build file** (format at the top of `src/scene/build_spec.h`) has three parts.
    `[game]` gives the name, the start scene, the version and the version code.
    `[environment.<name>]` tables give each environment's `release` flag, `debug_tools` and a
    `config` table, and `zaro build --env` picks one. `[[step]]` tables add steps.
  - **Steps** belong to the pre or post stage, and both the game and its plugins declare them; a
    plugin uses `[[step]]` in its build extension (D-026). In each stage the plugins' steps run
    first, in scene order, then the game's. A step either runs a program (`run`) or uses a step
    built into zaro (`uses`, with `with` arguments). The built-ins are `check`, `test`,
    `design-check` and `cook-svg` (P3's SVG sprite art, D-028), entries of `kBuiltinSteps`
    (`src/app/pipeline.cpp`); generated code from P4a is a `run` step of the backend plugin.
  - **No hidden scripts**: a `run` step starts its program directly, without a shell, in the
    directory of the file that declares it. A shell, or an interpreter given inline code (`-c`,
    `-e`), is refused, so what runs is what the file says. `zaro` as the program means the running
    binary. Arguments may name `{out}`, `{env}`, `{version}` and `{version_code}`.
  - **Stamping and configuration**: before the pre steps, the pipeline writes
    `<out>/build_info.toml`. It holds the game, the version, the version code (the file's value or
    `--version-code`), the commit (`git rev-parse` of the game's checkout), the environment,
    `release`, `debug_tools` and the environment's config. Targets pack it with the game.
  - **Release cannot debug**: an environment with `release = true` cannot set `debug_tools` or any
    config key starting with `debug` (ZE1002, at that line). Release signing (D-025) will require
    a release environment.
  - **Errors**: every problem is a diagnostic with file and line. ZE1000 covers the build file, a
    step and an unknown built-in; ZE1001 a failed step, with its exit code; ZE1002 the release rule.
    `zaro build --plan` validates everything and prints the stages and steps without running them.
- **Why**:
  - The owner's games need work both before the native build (cooked assets, generated clients,
    stamped versions, per-environment endpoints, gates) and after the export (plugin
    dependencies and sources, symbols, size reports). One ordered pipeline holds both, so no
    target grows its own scripts.
  - Text declarations keep every step reviewable and checkable like scenes, and an agent adds a
    step the same way it edits a scene.
  - Built-in steps run in-process, so gates such as scene tests fail with the same diagnostics as
    the commands they come from.
- **Rejected**:
  - Shell hooks (`pre.sh`, `post.sh`): hidden logic, different on every OS, and nothing checks
    them before they run.
  - Driving everything from Gradle or a Python script: the target's own tool would decide the
    order, and iOS and PC would need another one.
  - Per-step input and output tracking (a make-like graph): more than games need now. Steps are
    few and fast, and the native build and Gradle keep their own incremental builds.
- **Consequences**: every step that is not built in is a program the game or plugin repository
  ships, so those repositories carry their generators as code.
- **Android, the first target**: `zaro build --target android` runs the stages with the tools of
  the checkout zaro was built from:
  - native: `tools/build.py android`
  - export: `zaro export android`, packing `build_info.toml` and leaving the build file out
  - post: the build extensions, then the post steps, then `tools/android.py stage-post`
    (`native-debug-symbols.zip` and `size_report.json`)
  - package: `stage-package` (Gradle, then bundletool and the 16 KB checks)
  - sign: `stage-sign` (who signed the package; a release must not be debug-signed)

  `--release` needs a release environment, a version code and the upload key variables, all
  checked in validate (ZE1003). `tools/android.py aab|apk <build.toml>` runs it. A standalone
  `zaro export android` still applies the build extensions itself.

## D-028 SVG art: LunaSVG (with its PlutoVG), in the zaro tool's cook step only

- **Status**: accepted, 2026-11-02. Supersedes D-024.
- **Decision**: SVG character art is drawn into sprite atlases by LunaSVG 3.5.0 (MIT), with the
  PlutoVG 1.3.1 (MIT) it bundles. Both come from one pinned tarball and are compiled without
  exceptions and RTTI, like ZARO code. They live in the `cook` module, which is built only for the
  desktop `zaro` tool, never for Android, so the Android runtime contains neither. A sprite sheet
  lists SVG files as its frames (`scene/sprite_sheet.h`). `zaro cook` (and any run that needs the
  pixels) draws them into `.zaro-cache/<sheet>.png` next to the sheet, keyed by a content hash of
  the sheet, its SVG files and `cook::kCookVersion` (D-008). The Android export packs the cooked
  PNG, not the SVG files. Before drawing, the cook scans each file and reports what it would leave
  out as ZE0903 with file and line: text, filters and foreign content. `zaro check` runs the same
  scan.
- **Why D-024 was replaced**: D-024 was chosen for drawing `<style>` classes and `clipPath`. The
  PlutoSVG v0.0.8 source shows it does neither: `clipPath` is parsed but marked TODO and never
  applied, and `<style>` is an unknown element it skips. Illustrator, Figma and Inkscape exports
  rely on both, so art would have rendered wrong without any warning.
- **Why LunaSVG**:
  - It draws CSS style sheets, `clipPath`, `mask`, `use`/`symbol`, gradients, `pattern` and
    `marker`. Test `svg_cook` checks classes, a circular clip, a mask, two `<use>`s and a
    gradient pixel by pixel.
  - Its sources have no `throw`, `try`, `catch`, `dynamic_cast` or `typeid`. It builds with
    `/EHs-c- /GR-` and `-fno-exceptions -fno-rtti`.
  - It is the same author's library on the same PlutoVG, so the license duty below is D-024's,
    unchanged. It is about 270 KB of C++17 plus PlutoVG.
  - System fonts are never loaded (`LUNASVG_DISABLE_LOAD_SYSTEM_FONTS`), so a cook does not depend
    on the machine. Text is therefore reported instead of drawn.
- **License duty**: PlutoVG's scan converter and stroker are FreeType code under the FreeType
  License. A binary distribution of the `zaro` tool must state that it is based in part on the
  work of the FreeType Team. `THIRD_PARTY_NOTICES.md` holds that statement and every dependency's
  license; lint rule Z010 fails when a dependency in `cmake/ZaroDeps.cmake` is missing there.
- **Rejected**: PlutoSVG (above); nanosvg (zlib, but no `<style>`, `clipPath` or `use`, and
  unmaintained); ThorVG (MIT, its rasterizer also FreeType-derived, about 900 KB of C++ with Lottie);
  resvg (Apache-2.0, the most complete, but needs a Rust toolchain); librsvg and cairo (LGPL, D-012).
- **Not supported**: text (convert it to outlines) and filters such as drop shadows and blurs (bake
  them into shapes). Revisit with the owner's art if needed.

## D-029 First game: a light neon arena game, before the idle flagship

- **Status**: accepted, 2026-11-02. Changes the order in D-018; the idle game (P5) stays planned.
- **Decision**: the owner's first game on ZARO is a light arena game. Glowing geometric shapes
  (rectangles, lines, rings) with tweens, bloom and particles sweep away large crowds of enemies.
  It has no frame-by-frame 2D animation; motion comes from tweens and the simulation. The engine
  features it needs are phase P6 (docs/ROADMAP.md); the game itself is G1, in a private repository.
- **Why**:
  - It plays to what ZARO already does best: tens of thousands of enemies in a deterministic
    simulation with GPU culling (M1).
  - Its art is code and shaders, so it needs no art production to look finished. A few effects
    done well (bloom, additive glow, particles, easing, screen shake) give a polished result.
  - It exercises the whole path to a store release (Android build, Play bundle, UI, audio, save)
    with far less content than the idle game. That path is what the idle game needs too.
- **Consequences**: P6 comes before P5. The survivors-like crowd game and entity hierarchies move
  out of "Later" into P6 and G1. Online features join G1 when P4 provides them; G1 does not wait
  for them.

## D-030 HDR frame: RG11B10F where renderable, RGBA16F otherwise; bloom as a fragment chain

- **Status**: accepted, 2026-11-02, by the owner. Settles the open decision "Bloom and HDR format"
  of P6.
- **Decision**:
  - A scene with `Bloom` or `ScreenFx` renders its 3D view into a linear HDR target. Its format is
    RG11B10Float (4 bytes per pixel) where the device can render and blend it (the WebGPU feature
    `rg11b10ufloat-renderable`), and RGBA16Float (8 bytes) otherwise. `Bloom.format` can force
    either. The bloom levels use the same format.
  - Bloom is a chain of half-size levels drawn with fragment shaders: a 13-tap downsample whose
    first pass keeps only light above a soft threshold and weights boxes by brightness, then a 3x3
    tent upsample back up, each level blended into the one above. No compute.
  - The tonemap is the identity up to 0.8 and rolls off above it, bleaching light far above 1 toward
    white; it dithers, adds the flash, and writes the sRGB frame. The UI is drawn after it.
- **Why**:
  - Mobile GPUs are short of memory bandwidth, not arithmetic. At a phone's 2400x1080 the HDR target
    is 10 MB in RG11B10F and 21 MB in RGBA16F, and each pass that loads and stores it moves that
    twice; RG11B10F halves all of it. The frame needs no alpha, and the 5-6 bit mantissas are hidden
    by the tonemap's dither.
  - Measured on the desktop GPU, where bandwidth matters least (release, RTX 5070, Vulkan,
    1920x1080, 50,000 enemies as glowing shapes): main pass 0.121 ms with RGBA16F against 0.113 ms
    with RG11B10F; bloom 0.041 ms and tonemap 0.038 ms with either. With 20,000 cubes: 0.046 against
    0.037 ms. The bloom chain itself costs the same in both, since its levels are small.
  - Fragment passes suit tile-based mobile GPUs, which keep a pass's target on chip; a compute bloom
    would write through storage textures, and WebGPU allows RG11B10F there only with another
    optional feature. Fragment passes also run on every WebGPU backend, the web build included.
- **Consequences**:
  - Scenes without Bloom or ScreenFx draw straight into the frame, as before.
  - The phone measurement is still to be made with P1's device run: `python tools/android.py apk
    examples/neon/scene.toml`, then `python tools/android.py perf`. If bloom costs more than about
    2 ms there, start the chain at a quarter of the frame or use fewer levels.
  - The shapes and particles draw in passes of their own, each loading and storing the HDR target.
    On a tile-based GPU merging them into the crowd's pass would save that traffic; the RHI has one
    pass per renderer today.

## D-031 Gameplay from scripts: entities at run time, events, timers, hierarchies and tweens

- **Status**: accepted, 2026-11-02. Built in steps in P6 (docs/ROADMAP.md); each step's API is
  documented at the top of `src/script/script.h` and `src/scene/world.h` when it lands.
- **Decision**: a scene's entities can change while it runs, and scripts drive them. Every part
  runs in the fixed 60 Hz tick (D-005) and stays inside the Luau sandbox (D-009).
  - **Entities at run time**: every entity has an id, unique in the run and never reused; the
    file's entities are 1..n in file order. Scripts name an entity by id or by name. They spawn
    prefabs that the scene lists in `[scene] prefabs` by their `[prefab]` name, with field
    overrides, and destroy entities. A destroyed entity is gone for scripts at once and removed at
    the end of the tick, so ids and iteration stay valid while scripts run. Queries return ids in
    id order.
  - **Events and timers**: scripts emit named events and subscribe to them; events emitted in a
    tick are delivered in the same tick, in order. Timers count ticks, not wall time.
  - **Hierarchies**: a `Transform` component and a parent link; a parent moves, turns and scales
    its children. World transforms are computed each tick with deterministic arithmetic (no
    platform `sin`/`cos`).
  - **Tweens**: easing curves from a start value to an end value over seconds of tick time,
    started from scripts or from `[[tween]]` tables in scene and prefab files. They may change only
    visual data: components registered as visual (`ZARO_VISUAL_COMPONENT`, such as `Sprite`) and
    UI widgets.
  - **State hash**: the scene tests' state hash covers the crowd and every live entity's id, name
    and non-visual component fields. Visual components are left out, so how a scene looks (sprites,
    tweens, effects) never changes a golden hash.
- **Why**:
  - G1 (D-029) needs waves of spawned things, reactions to hits and pickups, timed phases, shapes
    that orbit the player, and motion that comes only from tweens. Today a script can only change
    fields of entities the file declares.
  - Ids instead of names let a script hold thousands of spawned entities without inventing names;
    names stay for the file's entities, which scripts and tests already use.
  - Removing entities at the end of the tick, and keeping ids in ascending order, makes iteration
    safe and lookups a binary search, with no handle table.
  - Prefabs listed in the scene are loaded and checked with it (`zaro check`), packed by the
    Android export and their sprite sheets loaded before the first frame. A misspelt prefab name is
    an error with "did you mean".
  - Tick-based events, timers and tweens give the same result at any frame rate and on any
    platform, which golden hashes on three CI platforms require. Restricting tweens to visual data
    keeps "visuals never feed the simulation" true by construction.
- **Consequences**: `Scene::entities` can grow and shrink during a run; code that keeps pointers
  into it rebuilds them when `Scene::generation` changes (the sprite layer does). Golden hashes of
  existing scene tests changed once when the state hash started to cover the entities. New error
  ids are ZE1200-ZE1299; script misuse of the new API stays ZE0501 with file and line.

## D-032 Audio: miniaudio for decoding and mixing, SDL3 for the output device

- **Status**: accepted, 2026-11-02. Settles the P6 open decision "Audio library".
- **Decision**: a new `audio` module mixes sound with miniaudio 0.11.26 (public domain or MIT-0,
  the user's choice; no notice required). miniaudio is built with `MA_NO_DEVICE_IO`: it decodes
  (WAV, Ogg Vorbis through its bundled stb_vorbis, MP3, FLAC), resamples and mixes, and owns no
  device. `platform` opens the output device through SDL3's audio subsystem and pulls mixed frames
  from a callback. `audio` may include only `core`.
  - **Sounds** are declared in a sound file (TOML): each has a name, a file, a volume group, a
    volume and a pitch range, and a voice limit and cooldown, so ten thousand hits in a tick play a
    few voices. Short sounds are decoded into memory when the scene loads; music is streamed and
    loops, with fades between tracks.
  - **Synthesized sounds**: instead of a file, a sound can be a tone (a waveform, a frequency
    glide, an attack and release) or a pattern of notes over several tracks, such as a music loop.
    The engine renders it into samples when the scene loads, and it plays like any other sound.
  - **Volume groups** are declared in the same file under one `master`. A group can follow a field
    of the game's save (D-033), which is the settings hook: a settings screen changes the field
    and the volume follows, and it is kept across runs without script code.
  - **Playing**: scripts call `audio.*` (play, music, volume); a scene starts its music with
    `[scene] music`, and a UI button plays a sound when clicked. What scripts ask for goes into a
    queue that the app hands to the mixer after the tick. Nothing the mixer does is read back by the
    simulation or a script (D-005).
  - **Null device**: headless runs, `--sim-only` and scene tests use a null device that records
    what was played and mixes nothing. A window on a machine without an output device falls back to
    it with a warning. Scene tests can expect a sound to have been played.
  - **Mobile**: output pauses while the app is in the background and resumes with it.
- **Why**:
  - miniaudio's engine already has what G1 needs (groups, streaming, resampling, fades, sound
    copies that share decoded data), in one C file with no exceptions or RTTI. Writing a mixer on
    top of SDL3 alone would mean owning all of that.
  - Taking only the mixer keeps OS audio APIs inside `platform`, like windows and input: a console
    port replaces SDL's output there and nothing else. SDL3 is already the platform layer on every
    target, and its Android backend (AAudio, OpenSL ES) is the one the rest of the app uses.
  - P6 asks for `examples/neon` built only from text assets, and G1's art is code (D-029).
    Synthesized sounds let its sound be text too, reviewable and diffable like its shapes, while
    real recordings still play from files when a game has them.
  - 0.11.26 rather than an older pin: it fixes the ends of sounds being cut off and a race when
    group volumes change during playback, both on the paths ZARO uses.
  - A record-only null device lets tests check sound behaviour on CI machines, which have no audio
    hardware, without making golden hashes depend on audio.
- **Consequences**: SDL is built with `SDL_AUDIO` on. `miniaudio.h` belongs to `src/audio/`
  (lint Z007). Sound files are packed by the Android export. Errors are ZE1300-ZE1349.

## D-033 Local save: declared fields in a TOML file, behind a store the backend can replace

- **Status**: accepted, 2026-11-02. Settles the P6 open decision "Save format".
- **Decision**: a game declares its save in a save file (TOML) that the scene names, and scripts
  read and write only the fields it declares.
  - **Declaration**: `[save]` with the game's id and a `version`, then one `[[field]]` per value:
    a name, a type (`int`, `number`, `bool`, `string`), a default, an optional range, a `merge`
    hint (`latest`, `max`, `min`) and `local = true` for values that belong to the device, such
    as volumes. `zaro check` validates it like a scene.
  - **Scripts**: `save.get(field)`, `save.set(field, value)` (type- and range-checked, "did you
    mean" for misspelt names) and `save.commit()`, which marks a consistent point to store.
    Nothing is written between commits.
  - **Store**: the engine keeps the save as typed values and hands committed values to a store.
    The built-in store writes `save.toml` in the platform's user data directory for the game (on
    Android the app's internal files): a temporary file renamed over the old one, the previous
    copy kept as `save.toml.bak` and read when the main file is damaged. The backend plugin (P4c)
    provides another store with its own authorities and conflict merge; games keep the same file
    and the same calls.
  - **Versions**: the file records the version it was written with. Loading an older file runs
    the declared `[[migrate]]` steps in order (`from` a version: rename, remove or set fields);
    fields added since get their defaults. A file from a newer version is neither loaded nor
    overwritten: the game runs on defaults and says so.
  - **Runs**: windowed runs use the store. Headless runs and scene tests keep the save in memory,
    starting from defaults or from a save file the test names, unless `--save-dir` points at a
    directory; scene tests can expect saved values.
- **Why**:
  - A declared field list is what lets a server store, validate and merge a save field by field
    later; an opaque blob from a script would leave merging to each game. The merge hint and
    `local` are written now so moving to the server needs no game change.
  - TOML keeps saves readable and diffable like every other ZARO asset (D-008) and reuses the
    scene loader's parser and `file:line` errors.
  - An explicit commit gives every store, including a queued server one, whole states instead of
    half-updated ones, and keeps writes off the per-tick path.
  - Declarative migrations can be checked by `zaro check` and tested without running a game; the
    common changes (a renamed or retired field, a new default) need no code.
  - Write-then-rename with a backup survives a crash or a killed app in the middle of a write,
    which is common on phones.
- **Consequences**: save values loaded before `on_start` are an input of the run, like the seed:
  a scene test that depends on them names its starting save. Lists and tables in saves are not
  supported yet; they are added as new field types. Errors are ZE1350-ZE1399.

## D-034 Crowd size at run time: an active count under a capacity reserved at start

- **Status**: accepted, 2026-11-02. First step of P6's crowd track (docs/ROADMAP.md).
- **Decision**: `CrowdSpawner.max` reserves room for that many enemies when the run starts (0:
  `count`), and `CrowdSpawner.count` becomes a value scripts change while the game runs, like
  `speed`. The app reads it back after each `on_tick` and the simulation adds or retires enemies
  before the step (`sim::set_enemy_count`).
  - **Growing**: new enemies appear at the arena edge, standing, at a point hashed from their index
    and the tick, as respawned enemies do. **Shrinking**: the enemies with the highest indices
    leave, without counting as hits or kills.
  - **Above max**: the count is kept at max, the component is set back to the count actually used,
    and the run says so once. A scene file whose `count` is above its `max` is ZE1400 in
    `zaro check` and in every run.
  - **State hash**: covers the live enemies only; the count is mixed in when it differs from the
    count at start, so runs that never change it keep their golden hashes.
- **Why**:
  - Survivors-like games grow waves from a few hundred enemies to thousands. Reserving the
    simulation arrays, the instance buffer and the GPU buffers once keeps a run free of
    reallocation and of a GPU resource rebuild mid-frame, and gives a known memory ceiling on
    phones.
  - Keeping live enemies in the first `count` slots lets every parallel loop, the spatial hash,
    the cull and the indirect draw run over a dense range with no per-enemy alive flag.
  - Retiring the highest indices and placing new ones from index and tick hashes is the same for
    any thread count (D-005) and needs no random state.
  - Clamping instead of failing keeps a game running when a wave formula overshoots; the message
    and the read-back value tell a script author what happened.
- **Consequences**: `CrowdSpawner` gained a field, and the state hash covers every gameplay field
  of every entity (D-031), so the golden hashes of scene tests with a `CrowdSpawner` changed once.
  Crowd errors are ZE1400-ZE1499.

## D-035 A scene camera: height, pitch, field of view across a chosen side, and what it follows

- **Status**: accepted, 2026-11-02. Third step of P6's crowd track (docs/ROADMAP.md).
- **Decision**: a `Camera` component (one per scene, the first counts) sets the 3D view. It looks at
  a point from `height` metres above it, tilted `pitch` degrees below the horizon from the +z side,
  with `fov` degrees across `fov_axis`: the vertical side, the horizontal side, or the shorter side
  of the screen. The point is the player's position (`follow = "player"`) or `target`, plus
  `offset`, eased toward with `lag` seconds; `fog` moves where distance fog starts. Without a
  Camera the view is as before: 30 m above and 24 m behind the player with a 0.87 rad vertical
  field of view, which the component's defaults reproduce. `zaro run --zoom` scales the distance
  either way.
  - `Camera` is a visual component: it is not in the state hash, scripts and tweens may change it
    while the game runs (zooming out as a wave grows), and the app eases it per frame, never per
    tick. The camera never turns about the vertical, so the screen's up stays -z and its right +x,
    also when it looks straight down.
- **Why**:
  - A portrait phone with a fixed vertical field of view sees about half the width a landscape
    screen does, too little around the player in a survivors-like game. A field of view across the
    shorter side keeps the width on a portrait phone and the height on a landscape screen, so one
    scene suits both orientations.
  - Height and pitch are what a designer tunes for an arena game (how much ground, how steep);
    eye offsets would make them derived numbers. A fixed yaw keeps shapes, sprites and particles,
    which face the camera's right vector, unchanged.
  - Easing on the frame clock is smooth at any frame rate and touches nothing the simulation
    reads, so determinism and golden hashes are unaffected (D-005).
- **Consequences**: the crowd shader's fog start comes from the frame constants. Shapes, sprites
  and particles use the same camera through `render::crowd_camera`.

## D-036 Enemy kinds: every spawner entity of the scene is a kind in one shared crowd

- **Status**: accepted, 2026-11-02. Second step of P6's crowd track (docs/ROADMAP.md).
- **Decision**: each entity of the scene file with a `CrowdSpawner` is a kind of enemy, in file
  order, at most 8 (more is ZE1400). All kinds live in one simulation: they chase the same player
  along the same flow field and push each other apart, and each has its own count and max
  (D-034), speed and radius. Spawners spawned while the game runs are not kinds; destroying a
  kind's spawner retires its enemies.
  - **Layout**: enemies are kept packed in kind order (kind 0's, then kind 1's, ...). When a kind
    grows or shrinks, the later kinds' enemies move up or down by that much.
  - **Looks**: a kind's look is the `CrowdShape` on its spawner, else the scene's first one, and
    likewise its hit effects are its `CrowdFx`. One indirect draw still draws every kind: the GPU
    gets one look and the range of enemies per kind, and each enemy finds its kind from its index.
    A `CrowdSprite` still gives all kinds one animation.
  - **Scripts**: `zaro.crowd_kill(x, z, radius[, max[, kind]])` returns the count and a table of
    counts by spawner name, and with `kind` sweeps only that kind. The engine event `crowd_hit`
    comes once per kind that reached the player in a tick, with `kind` set to its spawner's id.
  - **State hash**: unchanged for one kind; the kinds' counts enter it as D-034 describes.
- **Why**:
  - Survivors-like waves mix enemy kinds with different speeds and sizes that must still crowd and
    flow around each other. One simulation keeps one flow field, one spatial hash and one
    separation pass over all of them; separate crowds would walk through each other and double
    the cost.
  - Spawner entities are already how a scene declares a crowd, prefabs and scripts already name
    entities, and a look and hit effects sit on the same entity, so a kind is written like any
    other entity with no new table format.
  - Packing kinds in order keeps every loop dense and lets the renderer find an enemy's kind from
    its index with no per-enemy data on the GPU. Moving the later kinds on a count change costs a
    memmove of the enemies after the kind, between ticks, which is small next to a tick.
  - Eight kinds keep the kind table in one small buffer and the shader's lookup a short loop.
  - Hues and animation phases hash an enemy's index within its kind, so they do not jump when
    another kind changes size.
- **Consequences**: `--enemies` and a scene test's `enemies` override the first kind. Kills and
  hits are reported per kind. A kind with no `CrowdShape` in a scene with shapes looks like the
  first kind.

## D-037 App identity: package, title and orientation in the game build file

- **Status**: accepted, 2026-11-02. Extends D-027 and D-025.
- **Decision**: a game's build file names how the app appears to stores and players, and
  `zaro build` passes it to the target's export.
  - **Keys** in `[game]` (format at the top of `src/scene/build_spec.h`):
    - `package`: the app's id in stores, the Android application id. Two or more dot-separated
      parts of letters, digits and `_`, each starting with a letter (the rule
      `zaro export android --package` already used, now `scene::valid_package_name`).
    - `title`: the name under the app's icon, 1 to 100 bytes of UTF-8. It cannot hold control
      characters or `< > & " ' \`, and cannot start with `@` or `?` (`scene::valid_app_title`).
    - `orientation`: `"portrait"` or `"landscape"`. On Android these become `sensorPortrait` and
      `sensorLandscape`, so the device can still turn the screen upside down.
    Problems are ZE1000 at the key's line.
  - **Defaults** are the export's, unchanged: package `dev.zaro.<scene name>`, the scene's name as
    title, landscape.
  - **Release**: `zaro build --release` needs `[game] package` and stops with ZE1003 without it.
  - **Export**: `zaro export android` gains `--title`; the manifest label is its own placeholder
    (`@ZARO_TITLE@`), and the Gradle project name stays the scene's name.
- **Why**:
  - Google Play fixes the package name at the first upload, and a game cannot rename it later. A
    release built with the placeholder `dev.zaro.<scene name>` would bind the game to an id it
    does not own, so a release build refuses to guess.
  - Package, title and orientation are facts about the game, not about one build command. In the
    build file they are reviewed with the version and reused by every target (an iOS bundle id
    takes the same reverse-domain form).
  - Refusing the characters that need XML escaping keeps the title a plain substitution into the
    manifest. Android reads an attribute starting with `@` or `?` as a resource reference.
- **Consequences**: the title is UTF-8 from the build file. `--title` on the command line
  reaches the export as the shell passes it, which on Windows is the ANSI code page, so
  non-ASCII titles belong in the build file. Per-environment ids (a `.dev` suffix so a test build
  installs beside the store build) are not supported yet; they would be an environment key.

## D-038 An orthographic camera for a flat, top-down look

- **Status**: accepted, 2026-11-02. Extends D-035.
- **Decision**: `Camera.projection` is `perspective` (the default, D-035) or `orthographic`. An
  orthographic camera shows `extent` metres of the world across the side `fov_axis` names (with
  `shorter`, the shorter side of the screen, so a portrait phone and a landscape window show the
  same ground around the player) and has no foreshortening. With `pitch = 90` it looks straight
  down, so the arena reads as flat 2D. Height, follow, offset, lag and fog work as with a
  perspective camera; `zaro run --zoom` widens `extent`.
- **Why**: the first game's look is flat 2D rather than a tilted 3D view. Sizing the view in metres
  of the arena is how a designer thinks about a top-down screen, and spanning the shorter side
  keeps that amount on every aspect ratio. Keeping perspective the default leaves existing scenes,
  screenshots and golden hashes as they are, and the camera stays visual only (D-005).
- **Consequences**: from straight above, upright things (world sprites, `CrowdSprite` enemies,
  obstacle walls) are seen edge-on or from their tops; a flat game draws with shapes. A shape on
  the ground needs a small height (`position` y) to stay above the ground plane, as in perspective.

## D-039 Open arenas and a choice of ground pattern

- **Status**: accepted, 2026-11-09. Extends D-038.
- **Decision**: `CrowdArena.obstacles` (default true) turns the obstacles scattered from the seed on
  or off. With false the simulation has no blocked cell, so enemies and the player cross the arena
  freely. `ArenaLook.pattern` picks what is drawn on the ground: `grid` (the default: lines with a
  halo), `lines` (sharp only), `dots` at the crossings, or `none`.
- **Why**: a flat 2D game has no use for boxes, and from straight above they read as dark patches
  on the ground. Hiding them would still leave enemies walking around things nobody can see, so the
  switch is in the simulation rather than in the look. The defaults keep existing scenes and how
  their crowds move.
- **Consequences**: `obstacles` is gameplay, so `scene::world_hash` covers it: adding the field moved
  the golden hash of every scene with a `CrowdArena` once, and turning it off changes a scene's run,
  because the seed's random numbers then go to spawning alone and enemies no longer path around
  anything. `pattern` is visual only (D-005). There are no flat obstacles; a game that wants them
  later adds them as a new option.
## D-040 Solid particles: squares, circles and triangles that spin and shrink

- **Status**: accepted, 2026-11-09. Extends the particles of D-029.
- **Decision**: `ParticleEmitter` and `CrowdFx` get `shape` (`spark`, the default, or `square`,
  `circle`, `triangle`) and `spin`. A solid piece is drawn with a sharp edge and covers what is
  behind it, keeps its colour for its whole life, shrinks to nothing, and turns at 40..100% of `spin`
  in a random direction from its own starting angle. It faces the camera.
- **Why**: a flat look throws out solid pieces rather than light, and spawning `Shape` entities per
  hit would cost a scene entity for each piece, where a burst is 80 bytes and the compute pass does
  the rest. One pipeline serves both looks because its blending is premultiplied: a spark writes
  alpha 0, which adds light, and a piece writes its coverage.
- **Consequences**: `render::ParticleBurst` grows from 64 to 80 bytes; `struct Particle` stays at 64
  by using the two fields it had free. Pieces are visual only (D-005), so no golden hash moves, and
  a piece glows only when its `intensity` is above 1 under `Bloom`. Pieces are drawn in ring order,
  with no depth test, as sparks are.

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

Text input and IME, physics (Jolt candidate), the MCP server (local only, access-restricted).
(Audio and local save, with save-format versioning, are P6: D-032, D-033.) (Fonts, localization, game UI and
pointer/gamepad navigation were built in P2: D-019 to D-021 and D-023.)
