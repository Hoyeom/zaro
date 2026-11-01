// SVG art for sprite sheets, rasterized at cook time (D-025). svg.cpp is the only file that
// includes lunasvg.h. The cook module is built only for the zaro tool, never for Android, so no
// game binary contains the SVG renderer.
#pragma once

#include "core/diag.h"

#include <stdint.h>

namespace zaro::cook {

// Reports, with file and line, every element or attribute of an SVG file the cook cannot draw
// (text, filters, foreign content), and files that are missing or not SVG (ZE0903). Editor
// metadata (title, desc, metadata, namespaced elements such as sodipodi:namedview) is ignored.
// Returns false if anything was reported.
bool check_svg(const char* path, DiagList* diags);

// Draws an SVG file into a width x height RGBA8 image (straight alpha, rows top to bottom), the
// document scaled to fit and centred. Runs check_svg first; false (with diagnostics) if it fails.
bool rasterize_svg(const char* path, uint32_t width, uint32_t height, uint8_t* rgba, DiagList* diags);

}  // namespace zaro::cook
