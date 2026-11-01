---
name: zaro-fx
description: Visual effects for the neon look (P6, D-029, D-030) — the HDR frame with bloom and its formats, the post-processing passes and tonemap, and how effect components reach render without touching the simulation. Load before adding or changing a Bloom component or another effect component, before editing src/app/fx_layer, src/render/post_renderer or shaders/post.slang, before adding a pass that draws into the 3D view, and when a glow looks wrong or costs too much.
---

# Visual effects (P6)

Everything here is visual only. Effect components are plain data in `sim/fx_components.h`; the app
reads them each frame and hands plain settings to `render`. They never feed the simulation, and
`sim::state_hash` does not include them (D-005).

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
