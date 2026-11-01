// UI themes: named design tokens that screen and prefab files use instead of literal values
// (D-023). `zaro import-design` writes a theme from a Claude Design kit; it can also be written by
// hand.
//
//   [theme]
//   name = "hello"
//   themes = ["light", "dark"]   # colour themes; [color] holds the first, [color.<id>] the others
//
//   [color]
//   surface = "#f7f5f0"          # sRGB, "#RRGGBB" or "#RRGGBBAA"
//   [color.dark]
//   surface = "#1d1a17"
//
//   [length]                     # spacing, radius, stroke ... in design units (CSS px)
//   space-4 = 16
//
//   [type.button]                # one table per type style
//   size = 20
//   line_height = 1.2            # a multiple of the size
//   weight = 700
//   family = "sans"
//
//   [family]                     # CSS font stacks, as the kit names them (informational)
//   sans = "\"Noto Sans KR\", sans-serif"
//
// A screen names its theme with `theme = "theme.toml"` in [screen]. Then any colour or number
// field (vector elements too) may be written "$name": "$surface", "$space-4", "$button.size". The
// value is resolved when the file loads, in the first colour theme.
#pragma once

#include "core/diag.h"

#include <stdint.h>

namespace zaro::scene {

struct Theme;

// Loads a theme file. Problems are diagnostics with file and line; returns nullptr if any.
Theme* load_theme(const char* path, DiagList* diags);
void free_theme(Theme* theme);

const char* theme_name(const Theme* theme);
// False if the theme has no such token of that kind.
bool theme_color(const Theme* theme, const char* name, uint32_t* rgba);
bool theme_number(const Theme* theme, const char* name, double* value);
// Closest token name of that kind for "did you mean", or nullptr.
const char* suggest_color(const Theme* theme, const char* name);
const char* suggest_number(const Theme* theme, const char* name);

}  // namespace zaro::scene
