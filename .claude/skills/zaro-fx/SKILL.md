---
name: zaro-fx
description: Visual effects for the neon look (P6, D-029, D-030) — glowing shapes (Shape, CrowdShape, ArenaLook) with halos, additive blending, trails and hit flash, the HDR frame with bloom and its formats, the post-processing passes and tonemap, and how effect components reach render without touching the simulation. Load before adding or changing a Shape, CrowdShape, ArenaLook, Bloom or another effect component, before editing src/app/fx_layer, src/app/shape_layer, src/render/post_renderer, src/render/shape_renderer or shaders/post.slang, shape.slang or shape_sdf.slang, before adding a pass that draws into the 3D view, and when a glow looks wrong or costs too much.
---

# Visual effects (P6)

Everything here is visual only. Effect components are plain data in `sim/fx_components.h` and
`sim/shape_components.h`; the app reads them each frame and hands plain data to `render`. They
never feed the simulation, and `sim::state_hash` does not include them (D-005). `zaro schema` lists
every field.

## Glowing shapes

- **`Shape`** (any entity): a rect, circle, ring, regular polygon or line. In the world it lies flat
  on the ground at `position` (y is its height; local +y points to world -z); on the screen it is
  in pixels. `follow = "player"` draws it at the player's interpolated position plus `position` and
  hides the player's cube. `intensity` above 1 glows under `Bloom`; `glow` adds a halo of that width
  beyond the edge; `blend = "add"` adds light, `"alpha"` covers. `spin` turns it on the tick clock.
- **Trails**: `trail` seconds of positions sampled every tick, drawn as fading, thinning line
  segments behind the shape (world shapes only).
- **Hit flash**: `flash` (0..1) turns the shape white and falls by `flash_decay` per second on the
  tick clock, in `app::step_shapes`. Scripts set it with `zaro.set`; scene tests can expect it.
- **`CrowdShape`** (one per scene): the enemies as shapes instead of cubes, the first corner along
  the heading, each with its own hue within `hue_jitter`. Drawn by the crowd renderer in its "main"
  pass (`vs_enemy_shape` in shaders/crowd_draw.slang), GPU-culled like the cubes.
- **`ArenaLook`**: a dark ground with a glowing grid, obstacle and background colours.
- `render::ShapeRenderer` draws all `Shape`s in one "shapes" pass after the crowd and before the
  sprites: one draw for world shapes (depth-tested, no depth writes), one for screen shapes. Each is
  a quad shaded from a signed distance (`shaders/shape_sdf.slang`, shared with the crowd shader);
  `render::ShapeInstance` mirrors `Instance` in shaders/shape.slang (64 bytes).
- **Errors**: ZE1100 for a CrowdShape beside a CrowdSprite, and for a screen shape that follows the
  player (`zaro check` reports both).
- **Tests**: `shape_render_gpu` (every kind at known pixels), `scene_tests_fx` (flash on the tick
  clock), `check_crowd_two_looks`, `check_screen_shape_follow`, `screenshot_shapes`.

## The HDR frame and bloom (D-030)

A scene with a `Bloom` component (on any entity; the first one counts) renders its 3D view into a
linear HDR target instead of the frame. `render::PostRenderer` then:

1. **bloom**: downsamples into half-size levels (the first keeps only light above `threshold`, with a
   soft `knee`, and weights boxes by brightness so single hot pixels do not flicker), then walks back
   up with a tent filter, blending each level into the one above by `radius`;
2. **tonemap**: adds the glow times `intensity`, tonemaps into the frame (identity up to 0.8, then a
   smooth roll-off; light far above 1 bleaches toward white, so bright coloured light gets white
   cores), adds the flash and dithers.

The UI is drawn after it, straight into the frame, so it never glows. Without a `Bloom` component
the 3D view draws into the frame as before: no extra passes, and the same pixels.

- **Glow comes from colour above 1.** Colours written as `"#RRGGBB"` are at most 1, so they glow
  only with `threshold` below 1. Effects that want to glow (P6 shapes and particles) multiply their
  colour by an intensity.
- **Formats**: `format = "auto"` (default) uses `rg11b10f` where the GPU can render and blend it
  (`rhi::supports_format`), else `rgba16f`. `rg11b10f` is 4 bytes per pixel, half the bandwidth;
  it has no alpha, which the frame does not need. perf.json's `hdr` names the format used.
- **Passes**: all bloom levels are timed under one name, `bloom`; the tonemap is `tonemap`. Levels
  stop at 8 pixels, so `levels` above what fits costs nothing.
- **A pass that draws into the 3D view** takes the target from `fx_begin_frame` (nullptr means the
  frame) and creates its pipelines with `fx_scene_format`. `render::CrowdScene::target`,
  `render::SpriteDraw::target` and `attach_sprite_gpu`'s format are the examples.
- **Tests**: `post_render_gpu` (tests/test_post_render.cpp) draws a sprite into each renderable HDR
  format and checks that the glow lights pixels beside it, leaves far ones alone, and that without
  bloom colours pass through.
