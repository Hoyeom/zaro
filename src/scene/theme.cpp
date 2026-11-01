#include "scene/theme.h"

#include "core/reflect.h"

#include <tomlc17.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

namespace {

constexpr uint32_t kMaxTokens = 1024;

}  // namespace

struct ThemeColor {
    char name[64];
    uint32_t rgba;
};

struct ThemeNumber {
    char name[96];
    double value;
};

struct Theme {
    char path[260];
    char name[64];
    ThemeColor colors[kMaxTokens];
    uint32_t color_count = 0;
    ThemeNumber numbers[kMaxTokens];
    uint32_t number_count = 0;
};

namespace {

bool parse_hex(const char* text, uint32_t* rgba) {
    static const reflect::FieldDesc kColorField = {"color", reflect::FieldType::kColor, 0, "", 1.0, 0.0, nullptr};
    char err[200];
    return reflect::parse_text_value(kColorField, text, rgba, err, sizeof(err));
}

void add_color(Theme* t, const char* name, const toml_datum_t& v, DiagList* d) {
    uint32_t rgba = 0;
    if (v.type != TOML_STRING || !parse_hex(v.u.str.ptr, &rgba)) {
        diag_add(d, kErrSceneType, t->path, v.lineno, v.colno, "colour '%s' must be \"#RRGGBB\" or \"#RRGGBBAA\"", name);
        return;
    }
    if (t->color_count == kMaxTokens || strlen(name) >= sizeof(ThemeColor::name)) return;
    ThemeColor& c = t->colors[t->color_count++];
    snprintf(c.name, sizeof(c.name), "%s", name);
    c.rgba = rgba;
}

void add_number(Theme* t, const char* name, const toml_datum_t& v, DiagList* d) {
    if (v.type != TOML_INT64 && v.type != TOML_FP64) {
        diag_add(d, kErrSceneType, t->path, v.lineno, v.colno, "'%s' must be a number", name);
        return;
    }
    if (t->number_count == kMaxTokens || strlen(name) >= sizeof(ThemeNumber::name)) return;
    ThemeNumber& n = t->numbers[t->number_count++];
    snprintf(n.name, sizeof(n.name), "%s", name);
    n.value = v.type == TOML_INT64 ? (double)v.u.int64 : v.u.fp64;
}

template <typename T>
const char* suggest(const T* items, uint32_t count, const char* name) {
    const char** names = (const char**)malloc(sizeof(char*) * (count ? count : 1));
    for (uint32_t i = 0; i < count; ++i) names[i] = items[i].name;
    const char* hint = reflect::suggest_name(name, names, count);
    free(names);
    return hint;
}

}  // namespace

Theme* load_theme(const char* path, DiagList* d) {
    Theme* t = new Theme();
    snprintf(t->path, sizeof(t->path), "%s", path);
    FILE* f = fopen(path, "rb");
    if (!f) {
        diag_add(d, kErrSceneSyntax, path, 0, 0, "cannot open theme file");
        delete t;
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)size + 1);
    const size_t n = fread(text, 1, (size_t)size, f);
    text[n] = 0;
    fclose(f);
    const uint32_t before = d->count;
    toml_result_t r = toml_parse_named(text, (int)n, path);
    if (!r.ok) {
        diag_add(d, kErrSceneSyntax, path, 0, 0, "invalid TOML: %s", r.errmsg);
    } else {
        const toml_datum_t top = r.toptab;
        for (int32_t i = 0; i < top.u.tab.size; ++i) {
            const char* key = top.u.tab.key[i];
            const toml_datum_t& v = top.u.tab.value[i];
            if (v.type != TOML_TABLE) {
                diag_add(d, kErrSceneStructure, path, v.lineno, v.colno,
                         "unexpected top-level key '%s' (expected [theme], [color], [length], [type.*] or [family])",
                         key);
                continue;
            }
            if (strcmp(key, "theme") == 0) {
                const toml_datum_t name = toml_get(v, "name");
                if (name.type == TOML_STRING) snprintf(t->name, sizeof(t->name), "%s", name.u.str.ptr);
            } else if (strcmp(key, "color") == 0) {
                // Colours of the first theme; [color.<id>] tables hold the other themes.
                for (int32_t k = 0; k < v.u.tab.size; ++k) {
                    if (v.u.tab.value[k].type != TOML_TABLE) add_color(t, v.u.tab.key[k], v.u.tab.value[k], d);
                }
            } else if (strcmp(key, "length") == 0) {
                for (int32_t k = 0; k < v.u.tab.size; ++k) add_number(t, v.u.tab.key[k], v.u.tab.value[k], d);
            } else if (strcmp(key, "type") == 0) {
                for (int32_t k = 0; k < v.u.tab.size; ++k) {
                    const toml_datum_t& style = v.u.tab.value[k];
                    if (style.type != TOML_TABLE) continue;
                    for (int32_t p = 0; p < style.u.tab.size; ++p) {
                        if (style.u.tab.value[p].type == TOML_STRING) continue;  // family
                        char name[128];
                        snprintf(name, sizeof(name), "%s.%s", v.u.tab.key[k], style.u.tab.key[p]);
                        add_number(t, name, style.u.tab.value[p], d);
                    }
                }
            } else if (strcmp(key, "family") != 0) {
                diag_add(d, kErrSceneStructure, path, v.lineno, v.colno,
                         "unexpected table [%s] (expected [theme], [color], [length], [type.*] or [family])", key);
            }
        }
    }
    if (r.ok || r.__internal) toml_free(r);
    free(text);
    if (d->count != before) {
        delete t;
        return nullptr;
    }
    return t;
}

void free_theme(Theme* t) { delete t; }

const char* theme_name(const Theme* t) { return t->name; }

bool theme_color(const Theme* t, const char* name, uint32_t* rgba) {
    for (uint32_t i = 0; i < t->color_count; ++i) {
        if (strcmp(t->colors[i].name, name) == 0) {
            *rgba = t->colors[i].rgba;
            return true;
        }
    }
    return false;
}

bool theme_number(const Theme* t, const char* name, double* value) {
    for (uint32_t i = 0; i < t->number_count; ++i) {
        if (strcmp(t->numbers[i].name, name) == 0) {
            *value = t->numbers[i].value;
            return true;
        }
    }
    return false;
}

const char* suggest_color(const Theme* t, const char* name) { return suggest(t->colors, t->color_count, name); }
const char* suggest_number(const Theme* t, const char* name) { return suggest(t->numbers, t->number_count, name); }

}  // namespace zaro::scene
