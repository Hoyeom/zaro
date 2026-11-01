#include "cook/svg.h"

#include <lunasvg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::cook {

namespace {

// Elements LunaSVG draws or uses (its element table), minus text, which would need font files.
const char* const kDrawn[] = {"a",       "circle",  "clipPath", "defs",           "ellipse", "g",      "image",
                              "line",    "linearGradient", "marker", "mask", "path", "pattern", "polygon",
                              "polyline", "radialGradient", "rect",  "stop",           "style",   "svg",    "symbol",
                              "use"};
// Elements that carry no drawing; they and their contents are skipped.
const char* const kMetadata[] = {"title", "desc", "metadata"};

bool in_list(const char* name, size_t len, const char* const* list, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (strlen(list[i]) == len && memcmp(list[i], name, len) == 0) return true;
    }
    return false;
}

char* read_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)n + 1);
    *size = fread(text, 1, (size_t)n, f);
    text[*size] = 0;
    fclose(f);
    return text;
}

// The hint for an element the cook does not draw.
const char* hint_for(const char* name, size_t len) {
    if ((len == 4 && memcmp(name, "text", 4) == 0) || (len == 5 && memcmp(name, "tspan", 5) == 0) ||
        (len == 8 && memcmp(name, "textPath", 8) == 0)) {
        return "convert text to outlines (paths) before export";
    }
    if ((len == 6 && memcmp(name, "filter", 6) == 0) || (len > 2 && name[0] == 'f' && name[1] == 'e')) {
        return "bake the effect (shadow, blur, glow) into shapes before export";
    }
    return "export without it, or flatten it to paths";
}

// Scans the XML for elements and attributes the cook cannot draw. A small scanner, not a parser:
// LunaSVG parses the file afterwards; this only finds what it would leave out, with lines.
bool scan(const char* path, const char* text, DiagList* d) {
    const uint32_t before = d->count;
    int line = 1;
    int skip_depth = 0;  // > 0 inside metadata
    bool seen_svg = false;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            ++line;
            continue;
        }
        if (*p != '<') continue;
        // Comments, CDATA, processing instructions and the doctype hold no elements.
        const char* close = nullptr;
        if (strncmp(p, "<!--", 4) == 0) close = "-->";
        else if (strncmp(p, "<![CDATA[", 9) == 0) close = "]]>";
        else if (p[1] == '?') close = "?>";
        else if (p[1] == '!') close = ">";
        if (close) {
            const char* end = strstr(p, close);
            for (const char* q = p; *q && q != end; ++q) line += *q == '\n';
            if (!end) break;
            p = end + strlen(close) - 1;
            continue;
        }
        const bool closing = p[1] == '/';
        const char* name = p + (closing ? 2 : 1);
        size_t len = 0;
        while (name[len] && !strchr(" \t\r\n/>", name[len])) ++len;
        const char* tag_end = strchr(name, '>');
        if (!tag_end) break;
        const bool self_closing = tag_end > name && tag_end[-1] == '/';
        const bool namespaced = memchr(name, ':', len) != nullptr;
        if (closing) {
            if (skip_depth && !namespaced) --skip_depth;
        } else if (skip_depth || namespaced) {
            // Editor metadata (sodipodi:namedview, inkscape:*) and everything inside it.
        } else if (in_list(name, len, kMetadata, sizeof(kMetadata) / sizeof(kMetadata[0]))) {
            if (!self_closing) skip_depth = 1;
        } else if (!in_list(name, len, kDrawn, sizeof(kDrawn) / sizeof(kDrawn[0]))) {
            diag_add(d, kErrSvg, path, line, 1, "<%.*s> is not drawn by the SVG cook: %s", (int)len, name,
                     hint_for(name, len));
        } else {
            seen_svg = seen_svg || (len == 3 && memcmp(name, "svg", 3) == 0);
            // Attributes that point at filters, on any drawn element.
            for (const char* a = name + len; a < tag_end; ++a) {
                if ((a[0] == ' ' || a[0] == '\t' || a[0] == '\n' || a[0] == '\r') && strncmp(a + 1, "filter", 6) == 0 &&
                    (a[7] == '=' || a[7] == ' ')) {
                    diag_add(d, kErrSvg, path, line, 1, "<%.*s filter=...> is not drawn by the SVG cook: %s", (int)len,
                             name, hint_for("filter", 6));
                }
            }
            // A <style> sheet's text is CSS: skip to its end so selectors are not read as tags.
            if (len == 5 && memcmp(name, "style", 5) == 0 && !self_closing) {
                const char* end = strstr(tag_end, "</style");
                if (!end) break;
                for (const char* q = p; q != end; ++q) line += *q == '\n';
                p = end - 1;
                continue;
            }
        }
        for (const char* q = p; q != tag_end; ++q) line += *q == '\n';
        p = tag_end;
    }
    if (!seen_svg && d->count == before) diag_add(d, kErrSvg, path, 1, 1, "not an SVG file (no <svg> element)");
    return d->count == before;
}

}  // namespace

bool check_svg(const char* path, DiagList* d) {
    size_t size = 0;
    char* text = read_file(path, &size);
    if (!text) {
        diag_add(d, kErrSvg, path, 0, 0, "cannot open SVG file");
        return false;
    }
    bool ok = scan(path, text, d);
    if (ok && !lunasvg::Document::loadFromData(text, size)) {
        diag_add(d, kErrSvg, path, 0, 0, "invalid SVG (the file does not parse)");
        ok = false;
    }
    free(text);
    return ok;
}

bool rasterize_svg(const char* path, uint32_t width, uint32_t height, uint8_t* rgba, DiagList* d) {
    if (!check_svg(path, d)) return false;
    size_t size = 0;
    char* text = read_file(path, &size);
    auto doc = lunasvg::Document::loadFromData(text, size);
    free(text);
    const float w = doc->width(), h = doc->height();
    if (w <= 0.0f || h <= 0.0f) {
        diag_add(d, kErrSvg, path, 0, 0, "the SVG has no size (give it a viewBox or width and height)");
        return false;
    }
    const float scale = (float)width / w < (float)height / h ? (float)width / w : (float)height / h;
    const float tx = ((float)width - w * scale) * 0.5f, ty = ((float)height - h * scale) * 0.5f;
    lunasvg::Bitmap bitmap((int)width, (int)height);
    bitmap.clear(0x00000000);
    doc->render(bitmap, lunasvg::Matrix(scale, 0.0f, 0.0f, scale, tx, ty));
    bitmap.convertToRGBA();
    for (uint32_t y = 0; y < height; ++y) {
        memcpy(rgba + (size_t)y * width * 4, bitmap.data() + (size_t)y * (size_t)bitmap.stride(), (size_t)width * 4);
    }
    return true;
}

}  // namespace zaro::cook
