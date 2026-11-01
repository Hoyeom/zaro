#include "ui/context.h"

#include "core/png.h"
#include "ui/pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::ui {

namespace {

constexpr uint32_t kMaxImages = 256;

}  // namespace

struct FontName {
    char name[32];
    int font;
};

struct LoadedImage {
    char path[260];
    ImageInfo info;
};

struct Context {
    TextSystem* text = nullptr;
    FontName fonts[kMaxFonts];
    uint32_t font_count = 0;

    uint32_t image_size = 0;
    uint8_t* image_pixels = nullptr;
    ShelfPacker image_packer;
    LoadedImage images[kMaxImages];
    uint32_t image_count = 0;
    uint32_t image_dirty[4] = {0, 0, 0, 0};
    const scene::StringTable* strings = nullptr;
    const scene::UiState* state = nullptr;
};

Context* create_context(const ContextDesc& desc) {
    Context* c = new Context();
    c->text = create_text_system(desc.glyph_atlas_size);
    c->image_size = desc.image_atlas_size;
    c->image_pixels = (uint8_t*)calloc((size_t)desc.image_atlas_size * desc.image_atlas_size, 4);
    pack_reset(&c->image_packer, desc.image_atlas_size);
    return c;
}

void destroy_context(Context* c) {
    if (!c) return;
    destroy_text_system(c->text);
    free(c->image_pixels);
    delete c;
}

TextSystem* context_text(Context* c) { return c->text; }

bool add_font(Context* c, const char* name, const char* path, DiagList* diags) {
    const int font = load_font(c->text, path, diags);
    if (font < 0) return false;
    for (uint32_t i = 0; i < c->font_count; ++i) {
        if (strcmp(c->fonts[i].name, name) == 0) {
            c->fonts[i].font = font;
            return true;
        }
    }
    if (c->font_count == kMaxFonts) {
        diag_add(diags, kErrFontLoad, path, 0, 0, "cannot name more than %u fonts", kMaxFonts);
        return false;
    }
    FontName& f = c->fonts[c->font_count++];
    snprintf(f.name, sizeof(f.name), "%s", name);
    f.font = font;
    return true;
}

int find_font(const Context* c, const char* name) {
    for (uint32_t i = 0; i < c->font_count; ++i) {
        if (strcmp(c->fonts[i].name, name) == 0) return c->fonts[i].font;
    }
    return -1;
}

int load_image(Context* c, const char* path, DiagList* diags) {
    for (uint32_t i = 0; i < c->image_count; ++i) {
        if (strcmp(c->images[i].path, path) == 0) return (int)i;
    }
    if (c->image_count == kMaxImages) {
        diag_add(diags, kErrImageLoad, path, 0, 0, "cannot load more than %u images", kMaxImages);
        return -1;
    }
    uint8_t* rgba = nullptr;
    uint32_t w = 0, h = 0;
    if (!read_png_rgba8(path, &rgba, &w, &h)) {
        diag_add(diags, kErrImageLoad, path, 0, 0, "cannot load image (missing, or not a PNG file)");
        return -1;
    }
    uint32_t x = 0, y = 0;
    if (!pack(&c->image_packer, w, h, &x, &y)) {
        free_png(rgba);
        diag_add(diags, kErrImageLoad, path, 0, 0, "%ux%u image does not fit the %ux%u image atlas", w, h,
                 c->image_size, c->image_size);
        return -1;
    }
    for (uint32_t row = 0; row < h; ++row) {
        memcpy(c->image_pixels + ((size_t)(y + row) * c->image_size + x) * 4, rgba + (size_t)row * w * 4, (size_t)w * 4);
    }
    free_png(rgba);
    uint32_t* d = c->image_dirty;
    if (d[2] <= d[0]) {
        d[0] = x, d[1] = y, d[2] = x + w, d[3] = y + h;
    } else {
        if (x < d[0]) d[0] = x;
        if (y < d[1]) d[1] = y;
        if (x + w > d[2]) d[2] = x + w;
        if (y + h > d[3]) d[3] = y + h;
    }
    LoadedImage& img = c->images[c->image_count];
    snprintf(img.path, sizeof(img.path), "%s", path);
    const float inv = 1.0f / (float)c->image_size;
    img.info.width = w;
    img.info.height = h;
    img.info.uv[0] = (float)x * inv;
    img.info.uv[1] = (float)y * inv;
    img.info.uv[2] = (float)(x + w) * inv;
    img.info.uv[3] = (float)(y + h) * inv;
    return (int)c->image_count++;
}

ImageInfo image_info(const Context* c, int image) {
    return image >= 0 && (uint32_t)image < c->image_count ? c->images[image].info : ImageInfo{};
}

void set_strings(Context* c, const scene::StringTable* table) { c->strings = table; }

void set_ui_state(Context* c, const scene::UiState* state) { c->state = state; }

const char* resolve_text(const Context* c, const char* text, char* out, size_t size) {
    const char* value = text[0] == '@' && c->strings ? scene::find_string(c->strings, text + 1) : nullptr;
    scene::format_text(c->state, value ? value : text, out, size);
    return out;
}

void begin_frame(Context* c) { begin_frame(c->text); }

AtlasUpdate take_image_update(Context* c) {
    AtlasUpdate u;
    u.pixels = c->image_pixels;
    u.size = c->image_size;
    u.x0 = c->image_dirty[0];
    u.y0 = c->image_dirty[1];
    u.x1 = c->image_dirty[2];
    u.y1 = c->image_dirty[3];
    c->image_dirty[0] = c->image_dirty[1] = c->image_dirty[2] = c->image_dirty[3] = 0;
    return u;
}

}  // namespace zaro::ui
