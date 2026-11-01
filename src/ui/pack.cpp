#include "ui/pack.h"

namespace zaro::ui {

void pack_reset(ShelfPacker* p, uint32_t size) {
    p->size = size;
    p->shelf_count = 0;
    p->next_y = 0;
}

bool pack(ShelfPacker* p, uint32_t w, uint32_t h, uint32_t* x, uint32_t* y) {
    const uint32_t ww = w + p->pad;
    const uint32_t hh = (h + p->pad + 3u) & ~3u;
    if (ww > p->size) return false;
    for (uint32_t i = 0; i < p->shelf_count; ++i) {
        Shelf& s = p->shelves[i];
        if (s.h == hh && s.x + ww <= p->size) {
            *x = s.x;
            *y = s.y;
            s.x += ww;
            return true;
        }
    }
    if (p->shelf_count == ShelfPacker::kMaxShelves || p->next_y + hh > p->size) return false;
    Shelf& s = p->shelves[p->shelf_count++];
    s.y = p->next_y;
    s.h = hh;
    s.x = ww;
    p->next_y += hh;
    *x = 0;
    *y = s.y;
    return true;
}

}  // namespace zaro::ui
