---
name: zaro-fx
description: Visual effects for the neon look (P6, D-029, D-030) — glowing shapes (Shape, CrowdShape, ArenaLook) with halos, additive blending, trails and hit flash, GPU particles (ParticleEmitter, CrowdFx), screen shake and flash (ScreenFx), the scene camera (Camera), the HDR frame with bloom and its formats, the post-processing passes and tonemap, and how effect components reach render without touching the simulation. Load before adding or changing an effect component (Shape, CrowdShape, ArenaLook, Bloom, ScreenFx, CrowdFx, ParticleEmitter, Camera), before editing src/app/fx_layer, src/app/shape_layer, src/render/post_renderer, shape_renderer or particle_renderer, or shaders/post, shape, shape_sdf, particle or particles_* .slang, before adding a pass that draws into the 3D view, and when a glow looks wrong or costs too much.
---

# Visual effects (P6)

Everything here is visual only. Effect components are plain data in `sim/fx_components.h` and
`sim/shape_components.h`; the app reads them each frame and hands plain data to `render`. They
never feed the simulation, and `sim::state_hash` does not include them (D-005). `zaro schema` lists
every field.

## Glowing shapes

- **`Shape`** (any entity): a rect, circle, ring, regular polygon or line. In the world it lies flat
  on the ground at `position` (y is its height; local +y is world +z, and `rotation` turns +x toward
  +z as `Transform` does, which the camera shows as clockwise, like screen shapes); on the screen it is
  in pixels. `follow = "player"` draws it at the player's interpolated position plus `position` and
  hides the player's cube. `intensity` above 1 glows under `Bloom`; `glow` adds a halo of that width
  beyond the edge; `blend = "add"` adds light, `"alpha"` covers. `spin` turns it on the tick clock.
- **Trails**: `trail` seconds of positions sampled every tick, drawn as fading, thinning line
  segments behind the shape (world shapes only).
- **Hit flash**: `flash` (0..1) turns the shape white and falls by `flash_decay` per second on the
  tick clock, in `app::step_shapes`. Scripts set it with `zaro.set`; scene tests can expect it.
- **`CrowdShape`** (on a kind's spawner entity): the enemies as shapes instead of cubes, the first
  corner along the heading, each with its own hue within `hue_jitter`. Each kind of enemy (D-036)
  takes the CrowdShape on its own spawner, else the scene's first one. Drawn by the crowd renderer in
  its "main" pass (`vs_enemy_shape` in shaders/crowd_draw.slang), GPU-culled like the cubes; the
  looks go to the GPU as one `render::EnemyKind` per kind with the kind's range of enemies, and each
  enemy's hue comes from its index within its kind, so it keeps it when another kind grows.
- **`ArenaLook`**: a dark ground with a glowing grid, obstacle and background colours.
- `render::ShapeRenderer` draws all `Shape`s in one "shapes" pass after the crowd and before the
  sprites: one draw for world shapes (depth-tested, no depth writes), one for screen shapes. Each is
  a quad shaded from a signed distance (`shaders/shape_sdf.slang`, shared with the crowd shader);
  `render::ShapeInstance` mirrors `Instance` in shaders/shape.slang (64 bytes).
- **Errors**: ZE1100 for a CrowdShape beside a CrowdSprite, and for a screen shape that follows the
  player (`zaro check` reports both).
- **Tests**: `shape_render_gpu` (every kind at known pixels), `scene_tests_fx` (flash on the tick
  clock), `check_crowd_two_looks`, `check_screen_shape_follow`, `screenshot_shapes`.

## Particles, shake and flash

- **`ParticleEmitter`** (any entity): sparks at `rate` per second, plus `burst` at once; the app
  emits the burst and sets it back to 0 on the next tick, so a script fires one with
  `zaro.set(entity, "ParticleEmitter", "burst", 40)`. `follow = "player"` emits around the player.
- **`CrowdFx`** (on a kind's spawner; a kind without one uses the scene's first): when an enemy reaches the player (it vanishes and respawns
  at the edge, `sim::last_hits`), a burst of `particles` where it was, `shake` and `flash` added to
  `ScreenFx`, and `Shape.flash` raised to `player_flash` on shapes that follow the player. At most
  64 hit bursts per tick; more hits still add shake.
- **`ScreenFx`** (one per scene): `shake` is trauma 0..1, the camera moves by `shake_strength` times
  its square along two smooth noises; `flash` adds `flash_color` over the frame in the tonemap. Both
  fall by their decay per second on the tick clock (`app::step_fx`), so scene tests can expect them.
  A scene with ScreenFx renders through the HDR frame even without Bloom.
- `render::ParticleRenderer` keeps a ring buffer of `app::kParticleCapacity` (32768) particles. The
  CPU sends 64 bytes per burst (`render::ParticleBurst`); a "particles" compute pass integrates the
  live ones and writes the new ones with hashed random directions (shaders/particles_sim.slang); a
  "particles" render pass draws each as an additive streak along its motion, without a depth test
  (shaders/particles_draw.slang). Bursts are only queued when there is a GPU; the component values
  change the same way either way.
- **Tests**: `particle_render_gpu` (a burst emitted, then moved by a frame's time),
  `scene_tests_fx` (decay.test.toml, hits.test.toml), `check_sparks_scene`, `screenshot_sparks`.

## The camera (D-035)

- **`Camera`** (one per scene, the first counts): looks at a point from `height` metres above it,
  `pitch` degrees below the horizon, from the +z side; `fov` spans `fov_axis` (`vertical`,
  `horizontal`, or `shorter`, which keeps a portrait phone's width). The point is the player
  (`follow = "player"`) or `target`, plus `offset`, eased with `lag` seconds on the frame clock in
  `app::frame_camera`; `fog` is where distance fog starts. Without one, `render::CrowdScene::camera`
  is nullptr and the view is the old fixed one (30 m up, 24 m back, 0.87 rad vertical).
- `projection = "orthographic"` (D-038) shows `extent` metres across `fov_axis` with no
  foreshortening; with `pitch = 90` it is a flat top-down view (tests/data/fx/camera_flat.toml).
  Upright things (world sprites, CrowdSprite, obstacle walls) are edge-on from there.
- Every pass in the 3D view gets the camera from `render::crowd_camera` and
  `render::crowd_camera_forward`; the right vector is +x whenever a Camera is set.
- **Tests**: `scene_camera` (tests/test_camera.cpp: the defaults match the old view, the shorter
  side on portrait and landscape, straight down, orthographic), `screenshot_camera` and
  `screenshot_camera_flat` (540x960).

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
