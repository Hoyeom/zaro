---
name: zaro-scene
description: Scenes, prefabs and components, sprites and sprite sheets, Luau game scripts, scene tests with golden state hashes, and headless runs with perf.json — the file formats, adding a component, the script API and sandbox, when a golden hash may be updated, and what each perf field means. Load before editing a scene, prefab, sprite sheet or *.test.toml file or a .luau script, before adding or changing a component, before editing src/render/sprite_renderer or shaders/sprite.slang, before running zaro test --update, and when reading or comparing perf.json.
---

# Scenes, scripts and tests

## Scenes and components (D-017)

The file format is documented at the top of `src/scene/scene.h`. A scene is a `[scene]` table plus
`[[entity]]` tables. Each entity has a `name`, an optional `prefab` (path relative to the scene) and
`[entity.<Component>]` tables. Values start from the component's defaults, then the prefab, then the
entity, field by field.

To add a component:
1. Define a plain struct with default member initializers in the module that owns the behaviour (see
   `src/sim/crowd_components.h`).
2. Describe it with `ZARO_FIELD` or `ZARO_FIELD_RANGE` and `ZARO_COMPONENT` (`src/core/reflect.h`),
   with a doc string for the component and for every field.
3. Register it from `app::register_all_components` (`src/app/tools.cpp`).

Field types: `bool i32 u32 u64 f32 vec2 vec3 vec4 string color enum` (strings up to 127 bytes).
`color` is written `"#RRGGBB"` or `"#RRGGBBAA"` (sRGB) and `enum` as one of the names that
`zaro schema` lists under `choices`; both are `uint32_t` members (`ZARO_FIELD_ENUM` for enums).
`zaro check` reports every problem in one pass as `file:line:col: error[ZE04nn]` (ids in
`src/core/log.h`), with a "did you mean" hint for misspelt components and fields. `zaro schema`
prints every registered component as JSON; read it instead of guessing names.

## Sprites (P3)

A `Sprite` component shows one frame of a sprite sheet, either standing in the 3D view and facing
the camera (`space = "world"`, position in metres) or on the screen (`space = "screen"`, pixels
from the top-left). The sheet format is at the top of `src/scene/sprite_sheet.h`: one PNG, its
frames (a `cell` grid or `[[frame]]` rects with pivots), `pixels_per_unit` for the world size, and
named `[[animation]]`s.

- With `animation` set, the app sets `frame` every tick from the tick clock
  (`scene::animation_frame`), so a scene test can expect a frame after N ticks without a GPU. A
  script that changes `animation` restarts it from that tick.
- `zaro check` loads every sheet and reports problems as `file:line`: ZE0900 (sheet keys, frames,
  animations), ZE0901 (image missing, or a frame outside it), ZE0902 (an animation the sheet does
  not have, with "did you mean", or a `frame` past the last one).
- Drawing: `app/sprite_layer` builds one `render::SpriteInstance` per visible sprite, batched by
  sheet; `render::SpriteRenderer` draws one call per batch in the "sprites" pass after the crowd
  (world sprites depth-tested and cut out at half alpha, then screen sprites blended in order),
  before the UI. `SpriteInstance` and `SpriteFrameInfo` mirror `shaders/sprite.slang`.
- PNGs are decoded by `read_png_rgba8` (`core/png.h`); the sheet loader reads only the size.
- `CrowdSprite` on the spawner entity draws the crowd's enemies as one animation of a sheet
  (`examples/crowd`), animated on the GPU from the run's time, so it changes no component and no
  state hash. Without it they are cubes, as in `zaro run` without a scene.
- SVG sheets (D-028) list one SVG file per frame and a `size`. The `zaro` tool cooks them into
  `.zaro-cache/<sheet>.png` (gitignored) when a run needs pixels and the content hash changed;
  `zaro cook <scene|sheet> [--force]` does it ahead of time. Scene tests and `--sim-only` runs never
  cook. Text, filters and foreign content are ZE0903 with the SVG file's line (`zaro check` too);
  convert text to outlines and bake effects into shapes.

## Scripts (D-009)

Game scripts are Luau files. The API and the sandbox are documented at the top of
`src/script/script.h`: the `on_start()` and `on_tick(tick)` hooks, and `zaro.get`, `zaro.set`,
`zaro.has`, `zaro.tick` and `zaro.log`. `zaro.set` goes through reflection, so a script can change any
registered component field with the same type and range checks as a scene file.

A scene names its script with `script = "file.luau"` in `[scene]`. `zaro run <scene>` calls
`on_start()` once and `on_tick(tick)` before every simulation tick (tick 1 is the first). Values the
script changes are read back the same tick. Today these are `CrowdSpawner.speed` and
`PlayerController.speed`; count, arena and seed are fixed at start. `zaro check` also compiles the
script and runs its top level.

Scripts drive the scene's UI through `ui` (same header): `ui.set(key, value)` sets a view-model
value that texts show as `{key}` (`{key:,}` groups digits), `ui.get_field`/`ui.set_field` read and
change any widget field of a screen node (for example `Layout.visible` to open a popup), and
`ui.language(code)` switches the string tables after the call. A clicked button with an `action`
calls `on_ui(action, screen, node)` before the frame's ticks. The view model and the command queue
are `scene::UiState` (`src/scene/ui_state.h`), owned by the app's UI layer.

A failing script ends a headless run with exit code 1. In a window it is paused and reloads when the
file is saved.

- There is no `os`, `io`, `loadstring` or `require`. The standard library and `zaro` are read-only,
  and `math.random` is seeded from the scene. Scripts are deterministic.
- Errors carry file and line: ZE0500 for compile errors, ZE0501 for runtime errors including API
  misuse (with "did you mean" hints), and ZE0502 for a call over its time budget.
- The Luau VM is built with `LUA_USE_LONGJMP=1`, so script errors never cross ZARO code as C++
  exceptions. A `zaro.*` binding raises with `luaL_error`, which does not return. Bindings must
  therefore hold no objects with destructors.

## Scene tests

`zaro test` runs `*.test.toml` files (format at the top of `src/scene/test_spec.h`). It looks in the
given files or directories, or in `tests/` when none are given. Each test runs its scene for N ticks
without a GPU, with the scripted player, the scene's script and its UI. `[[click]]` entries click a
screen's button before a given tick's `on_tick`. It then checks the `[[expect]]` values (`equals`,
`min`, `max`; with `screen` + `node` a widget field, or `text` for what a node shows) and the
optional golden `state_hash`.

- Failures are `file:line` diagnostics: ZE0600 invalid test file, ZE0601 expectation not met,
  ZE0602 state hash differs, ZE0603 a click found no shown, enabled button (or one behind a modal).
  A script error fails the test as well.
- `zaro screenshot ... --click hud/settings_button` clicks on the first frames, so a screenshot can
  show a popup (give it `--frames 15` so the entrance has finished).
- `[[back]]` presses back before a tick, and `[[expect]] screen = ".." state = "open"` checks where a
  screen is in its lifecycle (`opening`, `entering`, `open`, `closing`, `closed`).
- `--update` writes the current hash into each file. Use it only when a behaviour change is
  intended, and say so in the commit.
- CI runs the tests on Windows, Linux and macOS (ARM). The same golden hash must hold on all three.
  A ZE0602 on only one OS means float results diverged on that platform (D-005).

## Headless runs and perf.json

`zaro run --headless` runs one simulation tick per frame with a scripted player, renders offscreen,
and keeps at most two frames in flight. `--perf PATH` writes:

| Field | Meaning |
|---|---|
| `fps`, `frame_ms` (`avg`, `p50`, `p99`, `max`) | wall time per frame over the measured frames (after `warmup_frames`) |
| `cpu_ms.sim`, `cpu_ms.render_prep` | average CPU time of the simulation tick and of instance upload + command recording |
| `gpu_ms` | average GPU time per timed pass (`cull`, `main`) and `total`; `null` without timestamp support |
| `draw_calls`, `entities`, `enemies`, `ticks`, `hits` | per-frame draw calls and simulation counters |
| `state_hash` | simulation state after the run; equal for equal seed, tick count and build |
| `adapter`, `backend`, `threads`, `resolution`, `mode` | run environment |
| `hdr` | the HDR frame's format (`rg11b10f`, `rgba16f`), or `null` without a `Bloom` component (`zaro-fx`) |

The run also prints `state_hash=<hex> ticks=<n>` on stdout.
