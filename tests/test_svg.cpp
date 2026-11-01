// SVG cook: stylesheet classes, clip paths, masks, symbols with <use> and gradients are drawn;
// text and filters are reported with their lines. No GPU. Runs from the repository root;
// argv[1] is a directory where the rasterized images are written as PNG for inspection.
#include "cook/svg.h"
#include "core/diag.h"
#include "core/png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace zaro;

namespace {

int g_failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

bool has_diag(const DiagList* d, ErrorId id, int line, const char* text) {
    for (uint32_t i = 0; i < d->count; ++i) {
        if (d->items[i].id == id && d->items[i].line == line && strstr(d->items[i].message, text)) return true;
    }
    return false;
}

const uint8_t* at(const uint8_t* rgba, uint32_t w, uint32_t x, uint32_t y) { return rgba + ((size_t)y * w + x) * 4; }

bool near(const uint8_t* p, int r, int g, int b, int a) {
    const int tol = 12;
    return abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol && abs(p[3] - a) <= tol;
}

void save(const char* dir, const char* name, uint32_t w, uint32_t h, const uint8_t* rgba) {
    char path[600];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    write_png_rgba8(path, w, h, rgba);
}

void test_style_and_clip(const char* out) {
    DiagList* d = new DiagList();
    uint8_t px[64 * 64 * 4];
    expect(cook::rasterize_svg("tests/data/svg/style_clip.svg", 64, 64, px, d), "style_clip.svg rasterizes");
    if (g_failures) diag_print_text(d, stderr);
    save(out, "svg_style_clip.png", 64, 64, px);
    expect(near(at(px, 64, 40, 40), 0xe0, 0x70, 0x30, 255), "a <style> class fills the shape");
    expect(at(px, 64, 2, 2)[3] == 0, "the clip path cuts the corner off");
    expect(near(at(px, 64, 22, 26), 0x20, 0x20, 0x20, 255), "a second class");
    delete d;
}

void test_mask_use_gradient(const char* out) {
    DiagList* d = new DiagList();
    // 128 x 64: the 64 x 32 document at twice its size.
    uint8_t* px = (uint8_t*)malloc(128 * 64 * 4);
    expect(cook::rasterize_svg("tests/data/svg/mask_use.svg", 128, 64, px, d), "mask_use.svg rasterizes");
    if (g_failures) diag_print_text(d, stderr);
    save(out, "svg_mask_use.png", 128, 64, px);
    expect(near(at(px, 128, 16, 16), 0x20, 0x80, 0xf0, 255), "first <use> of the symbol");
    expect(near(at(px, 128, 16, 48), 0x20, 0x80, 0xf0, 255), "second <use> of the symbol");
    expect(at(px, 128, 112, 32)[3] == 0, "the mask hides the right half of the square");
    const uint8_t* g = at(px, 128, 66, 32);
    expect(g[3] > 240 && g[0] > 200 && g[1] > 200, "the gradient starts white at its left edge");
    delete d;
    free(px);
}

void test_unsupported() {
    DiagList* d = new DiagList();
    expect(!cook::check_svg("tests/data/svg/unsupported.svg", d), "unsupported.svg is reported");
    expect(has_diag(d, kErrSvg, 5, "<filter>"), "<filter> at line 5");
    expect(has_diag(d, kErrSvg, 5, "<feGaussianBlur>"), "<feGaussianBlur> at line 5");
    expect(has_diag(d, kErrSvg, 6, "filter=..."), "filter attribute at line 6");
    expect(has_diag(d, kErrSvg, 7, "convert text to outlines"), "<text> at line 7 with a hint");
    expect(d->count == 4, "metadata, title and sodipodi:namedview are not reported");
    if (g_failures) diag_print_text(d, stderr);
    d->count = 0;
    uint8_t px[4 * 4 * 4];
    expect(!cook::rasterize_svg("tests/data/svg/none.svg", 4, 4, px, d), "a missing file fails");
    expect(d->count == 1 && d->items[0].id == kErrSvg, "missing file is ZE0903");
    delete d;
}

}  // namespace

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : ".";
    test_style_and_clip(out);
    test_mask_use_gradient(out);
    test_unsupported();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("svg cook: ok\n");
    return 0;
}
