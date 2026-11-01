// Sprite sheets: frames cut from one PNG image, and named animations over them (P3).
//
//   [sheet]
//   image = "slime.png"        # PNG (RGBA, straight alpha), relative to this file
//   pixels_per_unit = 64       # image pixels per world metre, for billboards in the 3D view
//   pivot = [0.5, 1.0]         # point of a frame placed at the sprite's position, 0..1 from top-left
//   cell = [64, 64]            # optional: the frames are the image's cells, row by row
//
//   [[frame]]                  # explicit frames, when there is no `cell`
//   rect = [0, 0, 64, 64]      # x, y, width, height in image pixels
//   pivot = [0.5, 1.0]         # optional, overrides the sheet's pivot
//
//   [sheet]                    # or SVG art (D-028): each file is one frame, in this order
//   svg = ["cat/walk_0.svg", "cat/walk_1.svg"]
//   size = [128, 128]          # pixels each SVG is drawn at (scaled to fit, centred)
//
// An SVG sheet's PNG is cooked by the zaro tool into .zaro-cache/<sheet name>.png next to the
// sheet, with the content hash of the sheet and its SVG files in .zaro-cache/<sheet name>.hash; a
// run cooks it again when the hash changes. The cache is gitignored (D-008), and the Android export
// packs the cooked PNG instead of the SVG files.
//
//   [[animation]]
//   name = "walk"
//   frames = [0, 1, 2, 3]      # frame indices, played in this order
//   fps = 8.0
//   loop = true                # false holds the last frame
//
// The loader reads a PNG's size (not its pixels), so every frame and animation is checked when the
// sheet loads; problems are file:line diagnostics (ZE0900, ZE0901). It does not read SVG files.
#pragma once

#include "core/diag.h"

#include <stdint.h>

namespace zaro::scene {

constexpr uint32_t kMaxSheetFrames = 1024;
constexpr uint32_t kMaxSheetAnimations = 32;
constexpr uint32_t kMaxAnimationFrames = 64;
constexpr uint32_t kMaxSvgFrames = 64;

struct SheetFrame {
    uint32_t x, y, w, h;  // pixels in the image
    float pivot[2];
};

struct SheetAnimation {
    char name[64];
    uint16_t frames[kMaxAnimationFrames];
    uint32_t frame_count;
    float fps;
    bool loop;
};

struct SpriteSheet {
    char path[260];
    char image[260];  // resolved path of the PNG (for SVG sheets, the cooked one)
    uint32_t image_width, image_height;
    // SVG sheets: the frames' files (resolved), the size they are drawn at, and where the cook
    // keeps the hash of what it drew from.
    char svg[kMaxSvgFrames][260];
    uint32_t svg_count;
    int svg_line;  // line of the svg array, for diagnostics about its files
    uint32_t frame_size[2];
    char hash_path[260];
    float pixels_per_unit;
    float pivot[2];
    SheetFrame frames[kMaxSheetFrames];
    uint32_t frame_count;
    SheetAnimation animations[kMaxSheetAnimations];
    uint32_t animation_count;
};

// Loads and checks a sheet. Every problem goes to diags; returns nullptr if there was any.
SpriteSheet* load_sprite_sheet(const char* path, DiagList* diags);
void free_sprite_sheet(SpriteSheet* sheet);

// Index of the named animation, or -1. suggest_animation gives the closest name for "did you mean".
int find_animation(const SpriteSheet* sheet, const char* name);
const char* suggest_animation(const SpriteSheet* sheet, const char* name);

// SVG sheets: the content hash of the sheet file and every SVG file (and the cook's version), as
// 16 hex digits. False if a file cannot be read.
bool svg_art_hash(const SpriteSheet* sheet, uint32_t cook_version, char out[17]);

// The sheet frame an animation shows `seconds` after it started, at `speed` times its fps.
// Pure arithmetic on the tick clock, so the result is the same on every platform.
uint32_t animation_frame(const SheetAnimation& animation, double seconds, float speed);

}  // namespace zaro::scene
