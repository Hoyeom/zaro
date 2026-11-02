# Roadmap

ZARO's first job is to run its owner's own games on PC and mobile (D-018). Live services
(accounts, saves, shop, mail, live-ops) come from the owner's existing backend platform through a
private plugin. This file is the plan that sessions pick work from. Each phase lists its scope, the
check that says it is done, what it waits for, and what can run beside it.

Status: M1 and M2 are done (docs/DECISIONS.md "M1 result", "M2 result").

## How a session works on a phase

- Read CLAUDE.md first. Branch as `type/short-name`, open one PR per logical step, and keep CI green.
  CI checks Windows, Linux and macOS, plus lint and repository rules.
- A step is finished when its PR is merged and the check in its "Done when" line passes on `main`.
- New engine modules need entries in `MODULE_DEPS` (tools/lint.py), `SCOPES` (tools/git_rules.py) and
  CLAUDE.md "Modules". New decisions get a D-number in docs/DECISIONS.md.
- Proprietary SDKs and the backend plugin never enter this repository (D-012, D-016). They live in
  their own private repositories and reach the engine through the plugin ABI (D-011).
- Update this file when a phase starts (who), changes scope, or finishes.

## Phases

### P1. Mobile builds: Android first, then iOS

- **Status**: Android in progress since 2026-11-01 (toolchain and packaging: D-022). The APK
  builds locally and in CI; the on-device check below is pending. Release bundles (D-025) build,
  validate and sign with a game's upload key locally and in CI (with a throwaway key). Plugins
  add Gradle dependencies, manifest entries and sources through build extensions (D-026).
  `zaro build` (D-027) runs every release build through one staged pipeline; Android is its
  first target. A game's build file sets its package name, title and orientation (D-037).
  Installing a bundle on a device is pending.
- **Scope**:
  - Android build of the engine and the `zaro` runtime: SDL3 Android backend, wgpu on Vulkan, the
    Slang to WGSL step at build time, and assets packed into the APK.
  - Touch input mapped to `platform::Input`.
  - A Gradle project export (`zaro export android <scene>`) that agents can drive from the CLI.
  - A CI job that builds the APK; running it needs a device and stays local.
  - A Google Play release build (D-025): an AAB signed with the game's upload key from environment
    variables, the version from the game, a native debug symbols file, 16 KB alignment checks, and
    export hooks through which a plugin adds Gradle dependencies, manifest entries and Java or
    Kotlin sources (P4c's billing bridge needs them).
  - iOS follows with Metal, an Xcode export and a CI build on macOS.
- **Done when**: `examples/crowd/scene.toml` runs on a mid-range Android phone with its script at a
  stable 60 fps with at least 20,000 enemies, and the headless perf report can be pulled with `adb`.
  For the release build: `python tools/android.py aab <build.toml> --release` produces an AAB signed with
  a game-supplied upload key plus a `native-debug-symbols.zip` built from the unstripped `.so`
  files, `bundletool validate` passes, every `.so` is 16 KB aligned, and the AAB installs on a
  device through `bundletool build-apks` and `install-apks` (the device part waits for a phone).
- **Waits for**: nothing.
- **Runs beside**: P2, P4a.

### P2. UI system

- **Scope**:
  - Text rendering with a dynamic glyph atlas that covers Korean (Hangul) and Latin.
  - Layout, the core widgets (label, image, button, scroll list, modal), and input focus for
    touch, mouse and gamepad.
  - Screens described in text files, in the same spirit as scenes: TOML or a small markup,
    validated by `zaro check` with `file:line` errors.
  - View-model binding from Luau.
  - A navigation policy for the screen stack, modals and back that follows the backend
    platform's UI navigation rules, so screens behave the same in every game.
  - Importing a Claude Design kit (tokens, components) into the UI theme.
- **Done when**: `examples/hello` gets a HUD and a settings popup built only from text files. The
  screens render in Korean and English, and `zaro screenshot` plus a scene test cover them.
- **Decisions**: D-019 (stb_truetype, dynamic glyph atlas, Noto Sans KR) and D-020 (screens are
  TOML node lists with reflected widget components).
- **Status**: started 2026-11-01. Steps, one PR each: 1 text and glyph atlas, 2 layout, widgets
  and input focus, 3 screen files and `zaro check`, 4 Luau view-model binding, 5 navigation
  policy, 6 design kit import.
  - Step 1 (text and glyph atlas): stb_truetype, Noto Sans KR bundled, `ui` module, RHI textures
    and blending, `render::UiRenderer`; tests `ui_text` and `ui_render_gpu`. Merged (#13).
  - Step 2 (layout, widgets, input focus): `ui::Screen` with Layout, Panel, Label, Image (PNG via
    stb_image), Button, ScrollList and Modal; pointer, wheel, touch and gamepad/keyboard
    navigation in `platform::Input`; `color` and `enum` reflection types; test `ui_screen`. Merged
    (#18).
  - Step 3 (screen files): `[screen]` files with `[[node]]`s and `parent`, UI vs scene components,
    string tables per language with `@key` texts, `zaro check` for screens, keys and images,
    `[scene] ui`, `--lang`; `examples/hello` has `ui/hud.toml`, `ui/settings.toml` (hidden until
    step 4 opens it) and `strings/ko.toml`, `en.toml`. Merged (#19).
  - Step 4 (Luau view-model binding): `ui.set`/`ui.get` values shown through `{name}`
    placeholders, `ui.get_field`/`ui.set_field` on screen nodes, `ui.language`, the `on_ui` hook;
    `[[click]]` and screen/text expectations in scene tests; `zaro screenshot --click`. A modal on
    an upper screen blocks the screens below. `examples/hello` opens and closes its popup, switches
    language and shows the player's speed; tests `settings`, `settings_close`, and screenshots of the
    popup in both languages. With this the P2 "done when" check is met; steps 5 and 6 remain.
    Merged (#20).
  - Step 5 (navigation policy, D-021): `ui::Navigator` with root and openable screens, back,
    close, home and reset, the opening/entering/open/closing lifecycle with fade and pop
    transitions, claims through `on_back`, keys for reuse; `open:<screen>`, `close`, `back` and
    `home` button actions; `ui.open`/`close`/`back`/`home`/`reset`/`ready`/`state`, the
    `on_back` and `on_screen` hooks; `[[back]]` and `state` in scene tests; `zaro check` reports
    `open:` actions that name no screen. Test `ui_navigation`; hello's popup is now an openable
    screen. Merged (#22).
  - Step 6 (design kit import, D-023): theme files with `$token` values in screens and prefabs;
    `zaro import-design` turns a Claude Design kit's tokens into a theme and its components into
    prefab stubs; `examples/hello/design` is a small kit and `ui/kit/` its import, checked in CI.
- **Result**: done 2026-11-01. `examples/hello` has a HUD and a settings popup built only from text
  files (screens, strings, theme, prefabs, script), in Korean and English, covered by
  `zaro screenshot` (`screenshot_hello_*`) and scene tests (`examples/hello/tests`).
- **Waits for**: nothing.
- **Runs beside**: P1, P4a. P3 shares the texture atlas work, so P3 starts after P2's atlas lands.

### P3. 2D: sprites and the cat art pipeline

- **Scope**:
  - Batched sprites from texture atlases: screen-space 2D, and billboards in the 3D view for 2.5D
    games.
  - PNG loading now, with KTX2 later (D-008).
  - Sprite animation, and a pipeline that turns the owner's SVG character art into atlases at build
    time.
- **Done when**: the crowd demo can draw its enemies as animated sprites instead of cubes at the M1
  frame rate, and an SVG character from the art pipeline appears in a scene.
- **Decisions**: D-028 (LunaSVG with its PlutoVG, in the `zaro` tool's cook step only), which
  supersedes D-024 (PlutoSVG, which does not draw `clipPath` or `<style>`).
- **Status**: done 2026-11-02. Steps, one PR each: 1 D-024, 2 batched sprites from PNG atlases
  (screen space and billboards), 3 the SVG cook and a scene with an SVG character, 4 the crowd's
  enemies as animated sprites.
  - Step 1 (D-024, the SVG rasterizer): merged (#26).
  - Step 2 (sprites): merged (#27). Sprite sheet files (PNG, grid or explicit frames, pivots,
    named animations on the tick clock), the `Sprite` component (world billboards and screen
    sprites), the "sprites" pass with one draw per sheet batch, `zaro check` for sheets
    (ZE0900-ZE0902), PNG decoding moved to `core`; tests `sprite_sheets`, `sprite_render_gpu`, `scene_tests_sprites`,
    `screenshot_sprites`.
  - Step 3 (SVG art, D-028): the `cook` module (LunaSVG, desktop only), SVG sheets cooked into
    `.zaro-cache/` by content hash, `zaro cook`, ZE0903 for text and filters with lines,
    `THIRD_PARTY_NOTICES.md` with lint Z010, the export packing cooked atlases (checked in a
    local APK); `examples/cats` with original cat art; tests `svg_cook`, `example_cats_cook`,
    `check_svg_art`, `example_cats_scene`, `screenshot_cats` and the cats scene tests. Merged (#30).
  - Step 4 (the crowd as sprites): the `CrowdSprite` component draws the enemies as upright,
    camera-facing sprites of one animation in the crowd's GPU-culled indirect draw (6 vertices
    instead of 36), each from its own phase and mirrored when it walks against the camera;
    `examples/crowd` uses the cats' walk; the CI APK job checks that the cooked atlas is packed
    and the SVG files are not; test `check_crowd_sprite_animation`.
- **Result**: done 2026-11-02. `examples/crowd` draws its 50,000 enemies as the animated SVG cat at
  the cube frame rate. Release build, RTX 5070, Vulkan, 1920x1080, 24 threads,
  `zaro run examples/crowd/scene.toml --headless --perf perf.json`, three runs each in one session:

  | Enemies drawn as | fps | frame avg / p99 ms | sim ms | GPU ms (main) | draw calls |
  |---|---|---|---|---|---|
  | cubes | 980 / 1034 / 1048 | 0.95-1.02 / 1.32-1.47 | 0.77-0.82 | 0.06 | 4 |
  | cat sprites | 974 / 986 / 1008 | 0.99-1.03 / 1.38-1.53 | 0.79-0.82 | 0.10 | 4 |

  The frame is bound by the simulation on the CPU, so both run at the same rate (M1: 791 fps).
  With 200,000 enemies both ran at 331 fps; the GPU spent 0.34 ms on sprites and 0.15 ms on cubes.
  All runs of a scene ended with the same `state_hash`: drawing does not touch the simulation.
  `examples/cats` shows the SVG cat in the 3D view and on the screen (`screenshot_cats`, scene
  tests in `examples/cats/tests`). The cooked atlas was checked in a locally built APK; running on
  a phone is still to do with P1's device check.
- **Waits for**: P2's atlas and texture support (done).
- **Runs beside**: P1, P4.

### P4. Backend plugin (private repository)

- **P4a, contract client generator**: in the backend repository, add a C++ target to the API
  contract generator next to the existing C# one. Generated models and routes must compile in
  ZARO's C++ subset (no exceptions, no RTTI), and a test compiles them the way the C# output is
  compiled today.
  - **Waits for**: nothing.
- **P4b, plugin foundation**: create a private `zaro-<backend>` plugin repository on the plugin ABI
  (D-011, which this phase finally builds). It holds the HTTP and WebSocket transport, the error
  taxonomy and retry, and the network recovery state machine, ported from the backend platform's
  client rules.
  - **Waits for**: P4a.
- **P4c, services**: port account (device key, guest, sign-in), save (local, server and queued
  authorities, conflict merge), shop (purchase journal, receipt flow) and bignum. Each one is
  checked against the backend's golden vectors. Native in-app purchase bridges follow: Google Play
  Billing through JNI, then StoreKit.
  - **Waits for**: P4b, and P1 for mobile key stores and stores.
- **P4d, run verification**: the backend verifies a ZARO game's runs by replaying its command log
  with the headless `zaro` runtime and comparing state hashes, as D-005 allows.
  - **Waits for**: P4b.
- **Done when**: `examples/hello` signs in as a guest, saves to the server, and buys a test product
  on Android against the backend's sandbox.

### P5. Flagship slice

- **Scope**: the first part of the owner's mobile idle game on ZARO, end to end on Android: the
  field, growth, eggs and pets, and the shop, with the backend. This is the proof that ZARO can carry
  the owner's real games. The official announcement waits for it (D-016, D-018).
- **Waits for**: P1, P2, P3, P4 and P6. G1 comes first (D-029).

### P6. Game-making features

What a small game needs that the engine does not have yet, aimed at G1's look (D-029): glowing
shapes, tweens and particles over thousands of enemies, with no frame-by-frame animation.

- **Scope**, in three tracks that can run side by side:
  - **Neon rendering**: an HDR frame with bloom, so bright colours glow; a shape renderer for
    rectangles, lines, rings and polygons with glow, additive blending and trails; GPU particles
    (compute) for hits and deaths; screen shake and hit flash. All of it is visual only and never
    feeds the simulation (D-005).
    **Done** (D-030): `Bloom` renders the 3D view into an HDR frame (RG11B10F where the GPU
    renders it, else RGBA16F) with a fragment bloom chain and a tonemap; `Shape` (rectangles,
    circles, rings, polygons, lines with halos, additive blending, trails and hit flash),
    `CrowdShape` and `ArenaLook`; GPU particles (`ParticleEmitter`, and `CrowdFx` on the crowd's
    hits); `ScreenFx` shake and flash. `examples/neon/scene.toml` holds the arena's look. The
    60 fps check on a mid-range phone waits for P1's device run.
  - **Gameplay from scripts**: Luau can spawn and destroy entities, query them, send and receive
    events and run timers, not only read and write fields. Entity hierarchies (a parent moves its
    children). Tweens with easing curves for visual values (scale, colour, position offsets, UI),
    started from scripts and from scene files.
    **Done** (D-031): spawning from `[scene] prefabs`, destroying and queries by id or name;
    events and timers in the fixed tick; `Transform` with parents and deterministic world
    transforms; tweens (25 eases) from `[[tween]]` tables, `zaro.tween` and `ui.tween`, on visual
    components only; the crowd for scripts (`zaro.crowd_kill`, `crowd_hit` events). The scene-test
    state hash covers every entity's gameplay fields. `examples/neon/neon.luau` is the arena's
    gameplay, tested by `examples/neon/tests/waves.test.toml`.
  - **Audio and local save**: sound effects and music with volume groups and a settings hook; a
    local save that a game declares and that later moves to the backend's save authorities (P4c)
    without changing game code.
    **Done** 2026-11-02 (D-032 audio: miniaudio mixing, SDL3 output; D-033 save: declared
    fields in TOML behind a replaceable store). Steps, one PR each: 1 the decisions, 2 audio,
    3 local save, 4 an example with sound, volume settings and a saved best score, then
    `examples/neon`'s sound and save.
    - Step 1 (D-032, D-033): merged (#46).
    - Step 2 (audio): the `audio` module on miniaudio, SDL3 output in `platform`, sound files with
      groups, file sounds and synthesized tones and note patterns, `[scene] sounds` and `music`,
      `Button.sound`, `audio.*`, a null mixer for headless runs and scene tests (`sound` and
      `volume` expectations), `zaro check` (ZE1300-ZE1302), sound files in the Android export
      (#47).
    - Step 3 (local save): save declarations with typed fields, merge hints, `local` and
      `[[migrate]]` steps, `[scene] save`, `save.get`/`set`/`commit`, the file store (atomic write,
      backup, newer saves kept) and the memory store behind `app::SaveStore`, `--save-dir`, sound
      groups that follow a save field, starting saves and `save` expectations in scene tests,
      `zaro check` (ZE1350-ZE1353); tests `save`, `check_save_scene`, `scene_tests_save`,
      `save_runs_*` (#48).
    - Step 4 (`examples/neon`): synthesized sounds for sweeps, elites, hits, waves, buttons and a
      music loop; a HUD with the score and the saved best score; a settings popup that changes the
      music and effects volumes in the save, which the sound groups follow; a version 1 save
      migrated by a scene test. Tests `examples/neon/tests` (`sound`, `settings`, `save_migration`;
      the golden hashes did not change), `screenshot_neon_settings`, `example_neon_save_*` (the
      best score across runs). With this the track is done; the phone check is P6's.
- **Done when**: `examples/neon`, a small arena where the player sweeps a few thousand glowing
  enemies, is built only from scenes, scripts and text assets. It has bloom, particles, tweens, sound
  and a saved best score, runs at a stable 60 fps on PC and on a mid-range Android phone, and is
  covered by scene tests and `zaro screenshot`.
- **Waits for**: nothing. Phone measurement shares P1's device run.
- **Runs beside**: P1 (iOS), P4.

### G1. First game: neon arena (private repository)

- **Scope**: the owner's first game on ZARO (D-029): a light arena game where glowing geometric
  shapes sweep away large crowds of enemies. It is built in its own private repository on public
  ZARO and released on Google Play first. Online features (leaderboards, accounts) come through the
  backend plugin when P4 has them; the game does not wait for them.
- **Waits for**: P6, and P1's Play release build.

## Later

iOS store release, the web (WebGPU) build, Steamworks, consoles through private porting layers, the
Luau type checker, KTX2 textures and mipmaps.

## Open decisions

None at the moment.

(The SVG rasterizer for P3 was settled as D-028; for P6 the HDR format and bloom method as D-030,
the audio library as D-032 and the save format as D-033.)
