// `zaro import-design`: a Claude Design kit's tokens.json to a ZARO theme (D-023), plus a UI prefab
// stub per kit component. The only file that includes cJSON.h.
#include "app/tools.h"

#include "core/log.h"
#include "platform/platform.h"

#include <cJSON.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

namespace {

constexpr int kMaxAliasDepth = 16;

struct Notes {
    uint32_t colors = 0, lengths = 0, styles = 0, skipped = 0;
};

bool read_text(const char* path, char** text) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    *text = (char*)malloc((size_t)size + 1);
    const size_t n = fread(*text, 1, (size_t)size, f);
    (*text)[n] = 0;
    fclose(f);
    return true;
}

const char* str(const cJSON* obj, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : nullptr;
}

// A TOML key: bare when it can be, quoted otherwise (dots would make nested tables).
void write_key(FILE* f, const char* key) {
    bool bare = key[0] != 0;
    for (const char* p = key; *p; ++p) {
        const char c = *p;
        bare = bare && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_');
    }
    if (bare) fputs(key, f);
    else fprintf(f, "\"%s\"", key);
}

void write_string(FILE* f, const char* s) {
    fputc('"', f);
    for (const char* p = s; *p; ++p) {
        if (*p == '"' || *p == '\\') fputc('\\', f);
        fputc(*p, f);
    }
    fputc('"', f);
}

void write_number(FILE* f, double x) {
    if (x == floor(x) && fabs(x) < 1e9) fprintf(f, "%.0f", x);
    else fprintf(f, "%.4g", x);
}

// "16px" or 16 -> 16; "1rem"/"1em" -> 16 (CSS default root size). False for %, vw and the like.
bool parse_length(const cJSON* v, double* px) {
    if (cJSON_IsNumber(v)) {
        *px = v->valuedouble;
        return true;
    }
    if (!cJSON_IsString(v)) return false;
    char* end = nullptr;
    const double x = strtod(v->valuestring, &end);
    if (end == v->valuestring) return false;
    if (!*end || strcmp(end, "px") == 0) *px = x;
    else if (strcmp(end, "rem") == 0 || strcmp(end, "em") == 0) *px = x * 16.0;
    else return false;
    return true;
}

double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

// Numbers inside "rgb(...)" and the like, separated by commas, spaces or '/'; '%' marks a percentage.
int function_args(const char* s, double* out, bool* percent, int max) {
    const char* p = strchr(s, '(');
    if (!p || !strchr(p, ')')) return 0;
    int n = 0;
    ++p;
    while (*p && *p != ')' && n < max) {
        while (*p == ' ' || *p == ',' || *p == '/') ++p;
        char* end = nullptr;
        const double x = strtod(p, &end);
        if (end == p) return 0;
        percent[n] = *end == '%';
        out[n++] = x;
        p = end + (*end == '%' ? 1 : 0);
        if (strncmp(p, "deg", 3) == 0) p += 3;
        while (*p == ' ') ++p;
    }
    return n;
}

// A kit colour value to "#rrggbbaa": hex, rgb()/rgba(), hsl()/hsla(). False for other functions.
bool to_hex(const char* s, char out[10]) {
    double rgb[3], a = 1.0;
    if (s[0] == '#') {
        const size_t len = strlen(s);
        unsigned v[4] = {0, 0, 0, 255};
        if (len == 4 || len == 5) {
            for (size_t i = 1; i < len; ++i) {
                unsigned d = 0;
                if (sscanf(s + i, "%1x", &d) != 1) return false;
                v[i - 1] = d * 17;
            }
        } else if (len == 7 || len == 9) {
            for (size_t i = 0; i < (len - 1) / 2; ++i) {
                if (sscanf(s + 1 + i * 2, "%2x", &v[i]) != 1) return false;
            }
        } else {
            return false;
        }
        snprintf(out, 10, "#%02x%02x%02x%02x", v[0], v[1], v[2], v[3]);
        return true;
    }
    double args[4];
    bool pct[4];
    const int n = function_args(s, args, pct, 4);
    if (n < 3) return false;
    if (n == 4) a = pct[3] ? args[3] / 100.0 : args[3];
    if (strncmp(s, "rgb", 3) == 0) {
        for (int i = 0; i < 3; ++i) rgb[i] = clamp01(pct[i] ? args[i] / 100.0 : args[i] / 255.0);
    } else if (strncmp(s, "hsl", 3) == 0) {
        const double h = fmod(fmod(args[0], 360.0) + 360.0, 360.0) / 60.0;
        const double sat = clamp01(args[1] / 100.0), l = clamp01(args[2] / 100.0);
        const double c = (1.0 - fabs(2.0 * l - 1.0)) * sat, x = c * (1.0 - fabs(fmod(h, 2.0) - 1.0)), m = l - c / 2.0;
        const int k = (int)h;
        const double r[6] = {c, x, 0, 0, x, c}, g[6] = {x, c, c, x, 0, 0}, b[6] = {0, 0, x, c, c, x};
        rgb[0] = r[k % 6] + m;
        rgb[1] = g[k % 6] + m;
        rgb[2] = b[k % 6] + m;
    } else {
        return false;  // oklch(), lab(), color() ... would need a colour-space conversion
    }
    snprintf(out, 10, "#%02x%02x%02x%02x", (unsigned)(rgb[0] * 255.0 + 0.5), (unsigned)(rgb[1] * 255.0 + 0.5),
             (unsigned)(rgb[2] * 255.0 + 0.5), (unsigned)(clamp01(a) * 255.0 + 0.5));
    return true;
}

// The value of a colour token in a theme: its own, else the first theme's; aliases followed.
const char* raw_color(const cJSON* tokens, const char* name, const char* theme, const char* first) {
    const cJSON* t = nullptr;
    cJSON_ArrayForEach(t, tokens) {
        const char* n = str(t, "name");
        if (!n || strcmp(n, name) != 0) continue;
        const cJSON* v = cJSON_GetObjectItemCaseSensitive(t, "value");
        if (cJSON_IsString(v)) return v->valuestring;
        const char* own = str(v, theme);
        return own ? own : str(v, first);
    }
    return nullptr;
}

bool resolve_color(const cJSON* tokens, const char* name, const char* theme, const char* first, char out[10]) {
    const char* v = raw_color(tokens, name, theme, first);
    for (int depth = 0; v && v[0] == '{' && depth < kMaxAliasDepth; ++depth) {
        char target[96];
        const size_t len = strlen(v);
        if (len < 3 || v[len - 1] != '}' || len - 2 >= sizeof(target)) return false;
        memcpy(target, v + 1, len - 2);
        target[len - 2] = 0;
        v = raw_color(tokens, target, theme, first);
    }
    return v && v[0] != '{' && to_hex(v, out);
}

void write_colors(FILE* f, const cJSON* color, Notes* notes) {
    const cJSON* themes = cJSON_GetObjectItemCaseSensitive(color, "themes");
    const cJSON* tokens = cJSON_GetObjectItemCaseSensitive(color, "tokens");
    const char* first = cJSON_GetArraySize(themes) ? str(cJSON_GetArrayItem(themes, 0), "id") : "";
    if (!first) first = "";
    const int theme_count = cJSON_GetArraySize(themes) ? cJSON_GetArraySize(themes) : 1;
    for (int ti = 0; ti < theme_count; ++ti) {
        const char* id = ti ? str(cJSON_GetArrayItem(themes, ti), "id") : first;
        if (!id) continue;
        if (ti) fprintf(f, "\n[color.%s]\n", id);
        else fputs("\n[color]\n", f);
        const cJSON* t = nullptr;
        cJSON_ArrayForEach(t, tokens) {
            const char* name = str(t, "name");
            char hex[10];
            if (!name) continue;
            if (!resolve_color(tokens, name, id, first, hex)) {
                if (!ti) {
                    printf("  skipped colour '%s': not hex, rgb() or hsl(), or an alias that does not resolve\n", name);
                    notes->skipped++;
                }
                continue;
            }
            // Later themes list only what differs from the first.
            char base[10];
            if (ti && resolve_color(tokens, name, first, first, base) && strcmp(base, hex) == 0) continue;
            if (strcmp(hex + 7, "ff") == 0) hex[7] = 0;  // opaque: "#rrggbb"
            write_key(f, name);
            fprintf(f, " = \"%s\"\n", hex);
            if (!ti) notes->colors++;
        }
    }
}

void write_type(FILE* f, const cJSON* type, Notes* notes) {
    const cJSON* families = cJSON_GetObjectItemCaseSensitive(type, "families");
    const cJSON* groups = cJSON_GetObjectItemCaseSensitive(type, "groups");
    const cJSON* g = nullptr;
    cJSON_ArrayForEach(g, groups) {
        const char* group_family = str(g, "family");
        const cJSON* s = nullptr;
        cJSON_ArrayForEach(s, cJSON_GetObjectItemCaseSensitive(g, "styles")) {
            const char* name = str(s, "name");
            double size = 0.0;
            if (!name || !parse_length(cJSON_GetObjectItemCaseSensitive(s, "fontSize"), &size) || size <= 0.0) {
                printf("  skipped type style '%s': no font size in px or rem\n", name ? name : "?");
                notes->skipped++;
                continue;
            }
            fputs("\n[type.", f);
            write_key(f, name);
            fputs("]\nsize = ", f);
            write_number(f, size);
            fputc('\n', f);
            // A line height in px becomes a multiple of the size; a unitless one already is.
            const cJSON* lh = cJSON_GetObjectItemCaseSensitive(s, "lineHeight");
            double line = 0.0;
            if (cJSON_IsNumber(lh) && lh->valuedouble < 10.0) line = lh->valuedouble;
            else if (cJSON_IsString(lh) && !strpbrk(lh->valuestring, "pxrem%")) line = atof(lh->valuestring);
            else if (parse_length(lh, &line)) line /= size;
            if (line > 0.0) {
                fputs("line_height = ", f);
                write_number(f, floor(line * 1000.0 + 0.5) / 1000.0);
                fputc('\n', f);
            }
            const cJSON* w = cJSON_GetObjectItemCaseSensitive(s, "fontWeight");
            const double weight = cJSON_IsNumber(w) ? w->valuedouble : (cJSON_IsString(w) ? atof(w->valuestring) : 0.0);
            if (weight > 0.0) fprintf(f, "weight = %.0f\n", weight);
            const char* family = str(s, "family") ? str(s, "family") : group_family;
            if (family) {
                fputs("family = ", f);
                write_string(f, family);
                fputc('\n', f);
            }
            notes->styles++;
        }
    }
    if (cJSON_GetArraySize(families)) {
        fputs("\n# Font stacks as the kit names them. ZARO draws with the fonts it loads (\"regular\", \"bold\");\n"
              "# bundle a family's TTF or OTF files to use it.\n[family]\n",
              f);
        const cJSON* fam = nullptr;
        cJSON_ArrayForEach(fam, families) {
            if (!cJSON_IsString(fam)) continue;
            write_key(f, fam->string);
            fputs(" = ", f);
            write_string(f, fam->valuestring);
            fputc('\n', f);
        }
    }
}

// spacing, radius and every other {tokens: [...]} family whose values are lengths or numbers.
void write_lengths(FILE* f, const cJSON* root, Notes* notes) {
    fputs("\n[length]\n", f);
    const cJSON* family = nullptr;
    cJSON_ArrayForEach(family, root) {
        if (!cJSON_IsObject(family) || !strcmp(family->string, "color") || !strcmp(family->string, "type") ||
            !strcmp(family->string, "meta")) {
            continue;
        }
        const cJSON* tokens = cJSON_GetObjectItemCaseSensitive(family, "tokens");
        if (!cJSON_IsArray(tokens)) continue;
        const cJSON* t = nullptr;
        cJSON_ArrayForEach(t, tokens) {
            const char* name = str(t, "name");
            double px = 0.0;
            if (!name) continue;
            if (!parse_length(cJSON_GetObjectItemCaseSensitive(t, "value"), &px)) {
                printf("  skipped %s token '%s': not a length or a number (ZARO has no equivalent yet)\n",
                       family->string, name);
                notes->skipped++;
                continue;
            }
            write_key(f, name);
            fputs(" = ", f);
            write_number(f, px);
            fputc('\n', f);
            notes->lengths++;
        }
    }
}

// "PriceButton" -> "price_button".
void snake_case(const char* name, char* out, size_t size) {
    size_t o = 0;
    for (const char* p = name; *p && o + 2 < size; ++p) {
        const bool upper = *p >= 'A' && *p <= 'Z';
        if (upper && p != name && o) out[o++] = '_';
        out[o++] = upper ? (char)(*p - 'A' + 'a') : *p;
    }
    out[o] = 0;
}

const char* widget_for(const char* component) {
    if (strstr(component, "Button")) return "Button";
    if (strstr(component, "Text") || strstr(component, "Label") || strstr(component, "Title")) return "Label";
    if (strstr(component, "Image") || strstr(component, "Icon")) return "Image";
    if (strstr(component, "List") || strstr(component, "Scroll")) return "ScrollList";
    return "Panel";
}

struct StubContext {
    const char* out_dir;
    uint32_t written;
    uint32_t kept;
};

// One prefab per kit component (components/<Comp>/README.md), never overwriting an existing one.
void write_stub(const char* readme, void* user) {
    StubContext* ctx = (StubContext*)user;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", readme);
    char* slash = strrchr(dir, '/');
    char* back = strrchr(dir, '\\');
    if (back && (!slash || back > slash)) slash = back;
    if (!slash) return;
    *slash = 0;
    const char* comp = strrchr(dir, '/');
    const char* comp_back = strrchr(dir, '\\');
    if (comp_back && (!comp || comp_back > comp)) comp = comp_back;
    comp = comp ? comp + 1 : dir;
    if (strcmp(comp, "Cover") == 0) return;  // the kit's cover page, not a component
    char snake[96], path[600];
    snake_case(comp, snake, sizeof(snake));
    snprintf(path, sizeof(path), "%s/%s.toml", ctx->out_dir, snake);
    if (FILE* existing = fopen(path, "rb")) {
        fclose(existing);
        ctx->kept++;
        return;
    }
    // The guideline's first sentence: the first line after the heading, up to its first period.
    char* text = nullptr;
    char summary[400] = "";
    if (read_text(readme, &text)) {
        const char* p = text;
        while (*p) {
            const char* eol = strchr(p, '\n');
            const size_t len = eol ? (size_t)(eol - p) : strlen(p);
            if (len && p[0] != '#' && p[0] != '\r') {
                snprintf(summary, sizeof(summary), "%.*s", (int)(len < 300 ? len : 300), p);
                break;
            }
            p += len + (eol ? 1 : 0);
        }
        free(text);
    }
    for (char* c = summary; *c; ++c) {
        if (*c == '\r') *c = 0;
    }
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "# %s, from the design kit: %s\n", comp, summary);
    fputs("# A UI prefab stub written by `zaro import-design`, which never overwrites it. Fill in the\n"
          "# widget with theme tokens (\"$name\" from theme.toml) and use it from screens with\n",
          f);
    fprintf(f, "# prefab = \"%s.toml\" on a [[node]].\n[prefab]\nname = \"%s\"\n\n[%s]\n", snake, comp, widget_for(comp));
    fclose(f);
    ctx->written++;
}

// Drops '\r', so a checkout with Windows line endings still compares equal.
void strip_cr(char* s) {
    char* o = s;
    for (const char* p = s; *p; ++p) {
        if (*p != '\r') *o++ = *p;
    }
    *o = 0;
}

bool same_file(const char* a, const char* b) {
    char *x = nullptr, *y = nullptr;
    const bool read = read_text(a, &x) && read_text(b, &y);
    if (read) {
        strip_cr(x);
        strip_cr(y);
    }
    const bool same = read && strcmp(x, y) == 0;
    free(x);
    free(y);
    return same;
}

}  // namespace

int import_design(const char* kit_dir, const char* out_dir, bool check) {
    char tokens_path[600];
    char* text = nullptr;
    snprintf(tokens_path, sizeof(tokens_path), "%s/project/tokens.json", kit_dir);
    if (!read_text(tokens_path, &text)) {
        snprintf(tokens_path, sizeof(tokens_path), "%s/tokens.json", kit_dir);
        if (!read_text(tokens_path, &text)) {
            log_error(kErrDesignKit, "%s: no tokens.json (or project/tokens.json) in the design kit", kit_dir);
            return 1;
        }
    }
    cJSON* root = cJSON_Parse(text);
    free(text);
    if (!cJSON_IsObject(root)) {
        log_error(kErrDesignKit, "%s: not valid JSON", tokens_path);
        cJSON_Delete(root);
        return 1;
    }
    char theme_path[600], out_path[640];
    snprintf(theme_path, sizeof(theme_path), "%s/theme.toml", out_dir);
    // --check writes next to the committed theme and compares.
    snprintf(out_path, sizeof(out_path), check ? "%s.check" : "%s", theme_path);
    FILE* f = fopen(out_path, "wb");
    if (!f) {
        log_error(kErrDesignKit, "%s: cannot write (does the directory %s exist?)", out_path, out_dir);
        cJSON_Delete(root);
        return 1;
    }
    const char* name = str(root, "name") ? str(root, "name") : "design kit";
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
    fputs("# Generated by `zaro import-design` from a Claude Design kit's tokens.json (D-023).\n"
          "# Change the kit and import it again rather than editing this file.\n[theme]\nname = ",
          f);
    write_string(f, name);
    fprintf(f, "\nversion = %d\n", cJSON_IsNumber(version) ? version->valueint : 1);
    const cJSON* color = cJSON_GetObjectItemCaseSensitive(root, "color");
    const cJSON* themes = cJSON_GetObjectItemCaseSensitive(color, "themes");
    fputs("themes = [", f);
    for (int i = 0; i < cJSON_GetArraySize(themes); ++i) {
        const char* id = str(cJSON_GetArrayItem(themes, i), "id");
        if (!id) continue;
        if (i) fputs(", ", f);
        write_string(f, id);
    }
    fputs("]\n", f);
    Notes notes;
    printf("importing %s (%s)\n", tokens_path, name);
    if (cJSON_IsObject(color)) write_colors(f, color, &notes);
    write_lengths(f, root, &notes);
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsObject(type)) write_type(f, type, &notes);
    const bool written = fclose(f) == 0;
    cJSON_Delete(root);
    if (!written) {
        log_error(kErrIo, "%s: cannot write", out_path);
        return 1;
    }
    if (check) {
        const bool same = same_file(out_path, theme_path);
        remove(out_path);
        if (!same) {
            log_error(kErrDesignKit, "%s is out of date with %s; run `zaro import-design %s %s`", theme_path,
                      tokens_path, kit_dir, out_dir);
            return 1;
        }
        printf("%s: up to date\n", theme_path);
        return 0;
    }
    char components[600];
    snprintf(components, sizeof(components), "%s/project/components", kit_dir);
    StubContext stubs{out_dir, 0, 0};
    if (!platform::find_files(components, "README.md", write_stub, &stubs)) {
        snprintf(components, sizeof(components), "%s/components", kit_dir);
        platform::find_files(components, "README.md", write_stub, &stubs);
    }
    printf("wrote %s: %u colours, %u lengths, %u type styles; %u kit values skipped\n", theme_path, notes.colors,
           notes.lengths, notes.styles, notes.skipped);
    printf("component prefabs: %u new stubs, %u kept as they were\n", stubs.written, stubs.kept);
    return 0;
}

}  // namespace zaro::app
