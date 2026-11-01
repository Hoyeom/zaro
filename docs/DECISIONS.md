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

- **Status**: accepted, 2026-11-02.
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

Text input and IME, audio (miniaudio candidate), save-format versioning, physics (Jolt
candidate), the MCP server (local only, access-restricted). (Fonts, localization, game UI and
pointer/gamepad navigation were built in P2: D-019 to D-021 and D-023.)
