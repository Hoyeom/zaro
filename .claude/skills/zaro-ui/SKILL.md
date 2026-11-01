---
name: zaro-ui
description: The game UI (D-019 to D-021, D-023) — the ui module's draw list and renderer, UTF-8 text layout and the glyph atlas, screen files of nodes and widgets, string tables and languages, images, navigation, themes from design kits, and the per-frame order of update and draw. Load before editing src/ui/, render::UiRenderer or shaders/ui.slang, before writing or changing a screen file, a theme or a strings/<language>.toml table, before running zaro import-design, and when text or a widget does not show as expected.
---

# Text and UI (D-019, D-020)

The `ui` module builds the frame's UI as a `ui::DrawList` of screen-space quads (pixels, origin
top-left). `render::UiRenderer` draws the list over the frame in one pass ("ui") and one draw call.
`ui::Quad`, `render::UiQuad` and `struct Quad` in `shaders/ui.slang` are the same 80 bytes; change
all three together.

- Text is UTF-8. `ui::draw_text` lays out lines (breaks at `\n` and after spaces, long words
  between characters) and rasterizes glyphs into the atlas on first use. Measuring needs no GPU and
  never touches the atlas.
- Call `ui::begin_frame` before drawing text each frame. A full atlas skips the glyphs that do not
  fit for that frame, then is cleared and refilled on the next one.
- Screen files (`[screen]` + `[[node]]`, format at the top of `src/scene/scene.h`) load through the
  scene loader; widgets are UI components (`ZARO_UI_COMPONENT`, `"kind": "ui"` in `zaro schema`)
  and cannot go on scene entities, nor scene components on nodes (ZE0401). A node's `parent` must be
  an earlier node (ZE0408). `[scene] ui = [...]` lists the screens drawn over the scene.
- Texts starting with `@` are keys into `strings/<language>.toml` (`src/scene/strings.h`), chosen by
  `[scene] strings`, `languages` (first is the default) and `--lang`. `zaro check` on a scene reports
  keys missing from any language (ZE0702, with a hint), tables that disagree (ZE0703), and missing
  images (ZE0701).
- The bundled font is Noto Sans KR (Regular, Bold; OFL 1.1). The build copies it to `<bin>/fonts/`.
  Font errors are ZE0700. PNG images go into a second, RGBA atlas (`ui::load_image`, ZE0701).
- `ui::Context` holds the named fonts (`regular`, `bold`) and both atlases. A `ui::Screen` is a tree
  of named nodes carrying widgets (`src/ui/widgets.h`: `Layout`, `Panel`, `Label`, `Image`, `Button`,
  `ScrollList`, `Modal`), which are reflected structs like scene components. The top of
  `src/ui/screen.h` describes layout, input and focus.
- Each frame: `ui::begin_frame(context)`, `ui::update_screen` (applies the input against the
  previous layout, then lays out), read the click and back events, `ui::draw_screen`. The app fills
  `ui::UiInput` from `platform::Input` (pointer, wheel, edge-triggered navigation).
- Navigation (D-021, top of `src/ui/navigator.h`): `[scene] ui` screens are roots, `[scene] screens`
  can be opened. Buttons with `open:<screen>`, `close`, `back` or `home` navigate by themselves;
  `app/ui_layer` runs the policy, the script hooks (`on_ui`, `on_back`, `on_screen`) and back at
  root. Screen files declare `dismissible`, `transition` (`none`, `fade`, `pop`),
  `transition_time` and `skippable`. A screen takes clicks only once its entrance is over.
- Themes (D-023, top of `src/scene/theme.h`): `theme = "kit/theme.toml"` in `[screen]` lets colour
  and number fields of the screen and its prefabs say `"$name"` (ZE0409 if unknown).
  `zaro import-design <kit> <out>` writes the theme from a Claude Design kit's `tokens.json` and a
  UI prefab stub per kit component, which it never overwrites; fill stubs in with tokens. Do not
  edit a generated theme: change the kit and import again (`--check` in CI fails otherwise).

Scripts reach the UI through the `ui` table and `on_ui` (`zaro-scene`, "Scripts"). Scene tests click
buttons with `[[click]]` and check widget fields and shown text (`zaro-scene`, "Scene tests").
