// SVG art for sprite sheets, rasterized at cook time (D-027). svg.cpp is the only file that
// includes lunasvg.h. The cook module is built only for the zaro tool, never for Android, so no
// game binary contains the SVG renderer.
#pragma once

#include "core/diag.h"

#include <stdint.h>

namespace zaro::scene {
struct SpriteSheet;
}

namespace zaro::cook {

// Part of every SVG sheet's content hash: bump it when the cook draws the same input differently
// (a LunaSVG update, a layout change), so cached atlases are cooked again.
constexpr uint32_t kCookVersion = 1;

// Reports, with file and line, every element or attribute of an SVG file the cook cannot draw
// (text, filters, foreign content), and files that are missing or not SVG (ZE0903). Editor
// metadata (title, desc, metadata, namespaced elements such as sodipodi:namedview) is ignored.
// Returns false if anything was reported.
bool check_svg(const char* path, DiagList* diags);

// Draws an SVG file into a width x height RGBA8 image (straight alpha, rows top to bottom), the
// document scaled to fit and centred. Runs check_svg first; false (with diagnostics) if it fails.
bool rasterize_svg(const char* path, uint32_t width, uint32_t height, uint8_t* rgba, DiagList* diags);

// check_svg on every SVG file of a sheet; a missing one is reported at the sheet's svg line.
bool check_svg_files(const scene::SpriteSheet* sheet, DiagList* diags);

// Draws every SVG frame of a sheet (scene/sprite_sheet.h) into its atlas: image_width x
// image_height RGBA8, each file at its frame's rect. Returns the pixels (free them with free()), or
// nullptr after diagnostics; every file is checked even after one fails.
uint8_t* cook_svg_sheet(const scene::SpriteSheet* sheet, DiagList* diags);

}  // namespace zaro::cook
