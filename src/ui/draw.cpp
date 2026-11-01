#include "ui/draw.h"

#include <stdlib.h>
#include <string.h>

namespace zaro::ui {

void reset_draw_list(DrawList* list, float width, float height) {
    list->count = 0;
    list->clip[0] = 0.0f;
    list->clip[1] = 0.0f;
    list->clip[2] = width;
    list->clip[3] = height;
}

void free_draw_list(DrawList* list) {
    free(list->quads);
    *list = DrawList{};
}

Quad* push_quad(DrawList* list) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 256;
        list->quads = (Quad*)realloc(list->quads, sizeof(Quad) * list->capacity);
    }
    Quad* q = &list->quads[list->count++];
    memset(q, 0, sizeof(*q));
    memcpy(q->clip, list->clip, sizeof(q->clip));
    return q;
}

void draw_rect(DrawList* list, float x0, float y0, float x1, float y1, uint32_t color, float radius, float border,
               uint32_t border_color) {
    Quad* q = push_quad(list);
    q->rect[0] = x0;
    q->rect[1] = y0;
    q->rect[2] = x1;
    q->rect[3] = y1;
    q->color = color;
    q->border_color = border_color;
    q->kind = kQuadShape;
    q->radius = radius;
    q->border = border;
}

}  // namespace zaro::ui
