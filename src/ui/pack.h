// Shelf packing for square atlases (glyphs, images). Internal to the ui module.
#pragma once

#include <stdint.h>

namespace zaro::ui {

struct Shelf {
    uint32_t y, h, x;  // top, height and first free column of one row
};

struct ShelfPacker {
    static constexpr uint32_t kMaxShelves = 1024;
    uint32_t size = 0;
    uint32_t pad = 1;  // empty texels right of and below each rectangle, so filtering never bleeds
    Shelf shelves[kMaxShelves];
    uint32_t shelf_count = 0;
    uint32_t next_y = 0;
};

void pack_reset(ShelfPacker* p, uint32_t size);
// Places a w x h rectangle on a shelf of its height class (multiples of 4), opening a new shelf
// when none has room. False when the atlas is full.
bool pack(ShelfPacker* p, uint32_t w, uint32_t h, uint32_t* x, uint32_t* y);

}  // namespace zaro::ui
