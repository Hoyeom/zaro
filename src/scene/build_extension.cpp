#include "scene/build_extension.h"

#include "core/path.h"
#include "core/reflect.h"
#include "scene/build_steps.h"

#include <tomlc17.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

namespace {

bool is_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

// group:artifact:version, each part made of letters, digits and . _ - (the version also +).
bool valid_dependency(const char* s) {
    int part = 0, length = 0;
    for (const char* c = s;; ++c) {
        if (*c == ':' || !*c) {
            if (length == 0) return false;
            ++part;
            length = 0;
            if (!*c) break;
            continue;
        }
        const bool ok = is_alnum(*c) || *c == '.' || *c == '_' || *c == '-' || (part == 2 && *c == '+');
        if (!ok) return false;
        ++length;
    }
    return part == 3;
}

// A dotted Java-style name with at least two segments: android.permission.VIBRATE.
bool valid_permission(const char* s) {
    int segments = 0;
    const char* c = s;
    while (*c) {
        if (!(is_alnum(*c) || *c == '_') || (*c >= '0' && *c <= '9')) return false;
        while (is_alnum(*c) || *c == '_') ++c;
        ++segments;
        if (*c == '.') ++c;
        else if (*c) return false;
    }
    return segments >= 2 && s[strlen(s) - 1] != '.';
}

// Returns an error message for a fragment that cannot be placed where it goes, or nullptr.
const char* check_fragment(const char* xml, bool application) {
    const char* begin = xml;
    while (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n') ++begin;
    const char* end = begin + strlen(begin);
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) --end;
    if (begin == end || *begin != '<' || end[-1] != '>') return "must be XML elements, starting with '<' and ending with '>'";
    if (strstr(xml, "@ZARO_")) return "must not contain @ZARO_ placeholders";
    if (strstr(xml, "<manifest") || strstr(xml, "</manifest")) return "must not open or close <manifest>";
    if (strstr(xml, "<application") || strstr(xml, "</application")) {
        return application ? "must not open or close <application>"
                           : "must not contain <application>; use the application key for its children";
    }
    return nullptr;
}

void read_string_list(const toml_datum_t& v, const char* what, bool (*valid)(const char*), const char* example,
                      char (*out)[kExtensionItemCapacity], uint32_t* count, const char* path, DiagList* d) {
    if (v.type != TOML_ARRAY) {
        diag_add(d, kErrSceneType, path, v.lineno, v.colno, "%s must be an array of strings", what);
        return;
    }
    for (int32_t i = 0; i < v.u.arr.size; ++i) {
        const toml_datum_t& e = v.u.arr.elem[i];
        if (e.type != TOML_STRING) {
            diag_add(d, kErrSceneType, path, e.lineno, e.colno, "%s must be an array of strings", what);
        } else if (*count == kMaxExtensionItems || strlen(e.u.str.ptr) >= kExtensionItemCapacity) {
            diag_add(d, kErrSceneRange, path, e.lineno, e.colno, "%s: at most %u entries of %u bytes", what,
                     kMaxExtensionItems, kExtensionItemCapacity - 1);
        } else if (!valid(e.u.str.ptr)) {
            diag_add(d, kErrBuildExtension, path, e.lineno, e.colno, "%s entry '%s' is not like '%s'", what,
                     e.u.str.ptr, example);
        } else {
            snprintf(out[(*count)++], kExtensionItemCapacity, "%s", e.u.str.ptr);
        }
    }
}

char* read_fragment(const toml_datum_t& v, const char* what, bool application, const char* path, DiagList* d) {
    if (v.type != TOML_STRING) {
        diag_add(d, kErrSceneType, path, v.lineno, v.colno, "%s must be a string of XML", what);
        return nullptr;
    }
    if (const char* error = check_fragment(v.u.str.ptr, application)) {
        diag_add(d, kErrBuildExtension, path, v.lineno, v.colno, "%s %s", what, error);
        return nullptr;
    }
    const size_t n = strlen(v.u.str.ptr);
    char* copy = (char*)malloc(n + 1);
    memcpy(copy, v.u.str.ptr, n + 1);
    return copy;
}

void read_android(const toml_datum_t& t, BuildExtension* x, const char* path, DiagList* d) {
    for (int32_t i = 0; i < t.u.tab.size; ++i) {
        const char* key = t.u.tab.key[i];
        const toml_datum_t& v = t.u.tab.value[i];
        if (strcmp(key, "dependencies") == 0) {
            read_string_list(v, "dependencies", valid_dependency, "com.example:library:1.0.0", x->dependencies,
                             &x->dependency_count, path, d);
        } else if (strcmp(key, "permissions") == 0) {
            read_string_list(v, "permissions", valid_permission, "android.permission.VIBRATE", x->permissions,
                             &x->permission_count, path, d);
        } else if (strcmp(key, "manifest") == 0) {
            x->manifest_xml = read_fragment(v, "manifest", false, path, d);
        } else if (strcmp(key, "application") == 0) {
            x->application_xml = read_fragment(v, "application", true, path, d);
        } else if (strcmp(key, "sources") == 0) {
            if (v.type != TOML_STRING || !v.u.str.ptr[0]) {
                diag_add(d, kErrSceneType, path, v.lineno, v.colno, "sources must be a directory path");
            } else {
                path_join_relative(path, v.u.str.ptr, x->sources, sizeof(x->sources));
                x->sources_line = v.lineno;
            }
        } else {
            static const char* const kKeys[] = {"dependencies", "permissions", "manifest", "application", "sources"};
            const char* hint = reflect::suggest_name(key, kKeys, 5);
            diag_add(d, kErrSceneStructure, path, v.lineno, v.colno,
                     "unexpected key '%s' in [android]%s%s%s (expected dependencies, permissions, manifest, "
                     "application or sources)", key, hint ? " (did you mean '" : "", hint ? hint : "",
                     hint ? "'?)" : "");
        }
    }
}

}  // namespace

BuildExtension* load_build_extension(const char* path, DiagList* d) {
    BuildExtension* x = (BuildExtension*)calloc(1, sizeof(BuildExtension));
    x->steps = (BuildStep*)calloc(kMaxBuildSteps, sizeof(BuildStep));
    snprintf(x->path, sizeof(x->path), "%s", path);
    FILE* f = fopen(path, "rb");
    if (!f) {
        diag_add(d, kErrSceneSyntax, path, 0, 0, "cannot open build extension file");
        free_build_extension(x);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)(size > 0 ? size : 0) + 1);
    const size_t n = fread(text, 1, (size_t)(size > 0 ? size : 0), f);
    text[n] = 0;
    fclose(f);
    const uint32_t before = d->count;
    toml_result_t r = toml_parse_named(text, (int)n, path);
    if (!r.ok) {
        diag_add(d, kErrSceneSyntax, path, 0, 0, "invalid TOML: %s", r.errmsg);
    } else {
        bool has_header = false;
        const toml_datum_t top = r.toptab;
        for (int32_t i = 0; i < top.u.tab.size; ++i) {
            const char* key = top.u.tab.key[i];
            const toml_datum_t& v = top.u.tab.value[i];
            if (strcmp(key, "build_extension") == 0 && v.type == TOML_TABLE) {
                has_header = true;
                const toml_datum_t name = toml_get(v, "name");
                if (name.type == TOML_STRING && name.u.str.ptr[0] && strlen(name.u.str.ptr) < sizeof(x->name)) {
                    snprintf(x->name, sizeof(x->name), "%s", name.u.str.ptr);
                } else {
                    diag_add(d, kErrSceneStructure, path, v.lineno, v.colno,
                             "[build_extension] needs name = \"...\" (at most %zu bytes)", sizeof(x->name) - 1);
                }
            } else if (strcmp(key, "android") == 0 && v.type == TOML_TABLE) {
                read_android(v, x, path, d);
            } else if (strcmp(key, "step") == 0) {
                read_build_steps(v, path, x->steps, &x->step_count, d);
            } else {
                diag_add(d, kErrSceneStructure, path, v.lineno, v.colno,
                         "unexpected top-level key '%s' (expected [build_extension], [android] or [[step]])", key);
            }
        }
        if (!has_header) diag_add(d, kErrSceneStructure, path, 1, 1, "a build extension needs a [build_extension] table");
    }
    if (r.ok || r.__internal) toml_free(r);
    free(text);
    if (d->count != before) {
        free_build_extension(x);
        return nullptr;
    }
    return x;
}

void free_build_extension(BuildExtension* x) {
    if (!x) return;
    free(x->steps);
    free(x->manifest_xml);
    free(x->application_xml);
    free(x);
}

}  // namespace zaro::scene
