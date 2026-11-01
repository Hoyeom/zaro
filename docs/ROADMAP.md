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
  builds locally and in CI; the on-device check below is pending.
- **Scope**:
  - Android build of the engine and the `zaro` runtime: SDL3 Android backend, wgpu on Vulkan, the
    Slang to WGSL step at build time, and assets packed into the APK.
  - Touch input mapped to `platform::Input`.
  - A Gradle project export (`zaro export android <scene>`) that agents can drive from the CLI.
  - A CI job that builds the APK; running it needs a device and stays local.
  - iOS follows with Metal, an Xcode export and a CI build on macOS.
- **Done when**: `examples/crowd/scene.toml` runs on a mid-range Android phone with its script at a
  stable 60 fps with at least 20,000 enemies, and the headless perf report can be pulled with `adb`.
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
- **Decisions**: D-024 (PlutoSVG on PlutoVG, in the `zaro` tool's cook step only).
- **Status**: started 2026-11-02. Steps, one PR each: 1 D-024, 2 batched sprites from PNG atlases
  (screen space and billboards), 3 the SVG cook and a scene with an SVG character, 4 the crowd's
  enemies as animated sprites.
  - Step 1 (D-024, the SVG rasterizer): merged (#26).
  - Step 2 (sprites): sprite sheet files (PNG, grid or explicit frames, pivots, named animations
    on the tick clock), the `Sprite` component (world billboards and screen sprites), the
    "sprites" pass with one draw per sheet batch, `zaro check` for sheets (ZE0900-ZE0902), PNG
    decoding moved to `core`; tests `sprite_sheets`, `sprite_render_gpu`, `scene_tests_sprites`,
    `screenshot_sprites`.
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
- **Waits for**: P1, P2, P3 and P4.

## Later

iOS store release, the web (WebGPU) build, a crowd-action game (the survivors-like spin-off),
Steamworks, consoles through private porting layers, the Luau type checker, and entity hierarchies.

## Open decisions

None at the moment. (The SVG rasterizer for P3 was settled as D-024.)
