#include "scene/scene.h"

#include "core/path.h"
#include "scene/build_extension.h"
#include "scene/theme.h"

#include <tomlc17.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

using reflect::ComponentDesc;
using reflect::FieldDesc;
using reflect::FieldType;

namespace {

constexpr uint32_t kMaxEntities = 4096;
constexpr uint32_t kMaxComponentsPerEntity = 32;
constexpr uint32_t kMaxPrefabs = 64;

struct Loaded {
    toml_result_t result{};
    char* text = nullptr;
};

bool load_toml(const char* path, Loaded* out, DiagList* diags, ErrorId missing_id, const char* referrer,
               int ref_line, int ref_col) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        if (referrer) {
            diag_add(diags, missing_id, referrer, ref_line, ref_col, "cannot open '%s'", path);
        } else {
            diag_add(diags, missing_id, path, 0, 0, "cannot open file");
        }
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    out->text = (char*)malloc((size_t)size + 1);
    const size_t n = fread(out->text, 1, (size_t)size, f);
    out->text[n] = 0;
    fclose(f);
    out->result = toml_parse_named(out->text, (int)n, path);
    if (!out->result.ok) {
        // tomlc17 messages start with "(line N)"-style positions; keep them verbatim.
        diag_add(diags, kErrSceneSyntax, path, 0, 0, "invalid TOML: %s", out->result.errmsg);
        return false;
    }
    return true;
}

void free_loaded(Loaded* l) {
    if (l->result.ok || l->result.__internal) toml_free(l->result);
    free(l->text);
    l->text = nullptr;
}

const char* kind_name(toml_type_t t) {
    switch (t) {
        case TOML_STRING: return "string";
        case TOML_INT64: return "integer";
        case TOML_FP64: return "float";
        case TOML_BOOLEAN: return "boolean";
        case TOML_ARRAY: return "array";
        case TOML_TABLE: return "table";
        default: return "date/time";
    }
}

bool is_number(const toml_datum_t& v) { return v.type == TOML_INT64 || v.type == TOML_FP64; }
double number(const toml_datum_t& v) { return v.type == TOML_INT64 ? (double)v.u.int64 : v.u.fp64; }

bool in_range(const FieldDesc& f, double x) { return f.min > f.max || (x >= f.min && x <= f.max); }

bool is_token(const toml_datum_t& v) { return v.type == TOML_STRING && v.u.str.ptr[0] == '$'; }

// Resolves a "$name" theme token (D-023) for a colour or number field. On success a number token
// becomes a TOML number in *out (an integer when the value is whole); false after a diagnostic.
bool resolve_token(const toml_datum_t& v, bool color, const Theme* theme, const ComponentDesc* comp,
                   const FieldDesc& f, uint32_t* rgba, toml_datum_t* out, const char* file, DiagList* diags) {
    const char* name = v.u.str.ptr + 1;
    if (!theme) {
        diag_add(diags, kErrSceneToken, file, v.lineno, v.colno,
                 "%s.%s = \"%s\" is a theme token, but the file has no theme (theme = \"...\" in [screen])",
                 comp->name, f.name, v.u.str.ptr);
        return false;
    }
    double number = 0.0;
    if (color ? theme_color(theme, name, rgba) : theme_number(theme, name, &number)) {
        if (!color) {
            *out = v;
            const bool whole = number == (double)(int64_t)number;
            out->type = whole ? TOML_INT64 : TOML_FP64;
            if (whole) out->u.int64 = (int64_t)number;
            else out->u.fp64 = number;
        }
        return true;
    }
    const char* hint = color ? suggest_color(theme, name) : suggest_number(theme, name);
    diag_add(diags, kErrSceneToken, file, v.lineno, v.colno, "%s.%s: theme '%s' has no %s token '%s'%s%s%s", comp->name,
             f.name, theme_name(theme), color ? "colour" : "number", name, hint ? " (did you mean '$" : "",
             hint ? hint : "", hint ? "'?)" : "");
    return false;
}

// Writes one TOML value into a component field. Returns false (with a diagnostic) on mismatch.
bool assign_field(const ComponentDesc* comp, const FieldDesc& f, const toml_datum_t& in, void* data,
                  const Theme* theme, const char* file, DiagList* diags) {
    uint8_t* dst = (uint8_t*)data + f.offset;
    toml_datum_t v = in;
    const bool numeric = f.type == FieldType::kI32 || f.type == FieldType::kU32 || f.type == FieldType::kU64 ||
                         f.type == FieldType::kF32;
    if (is_token(v) && f.type == FieldType::kColor) {
        uint32_t rgba = 0;
        if (!resolve_token(v, true, theme, comp, f, &rgba, nullptr, file, diags)) return false;
        *(uint32_t*)dst = rgba;
        return true;
    }
    if (is_token(v) && numeric && !resolve_token(in, false, theme, comp, f, nullptr, &v, file, diags)) return false;
    const char* tn = reflect::type_name(f.type);
    auto type_error = [&]() {
        diag_add(diags, kErrSceneType, file, v.lineno, v.colno, "%s.%s expects %s, got %s", comp->name, f.name, tn,
                 kind_name(v.type));
        return false;
    };
    auto range_error = [&](double x) {
        diag_add(diags, kErrSceneRange, file, v.lineno, v.colno, "%s.%s = %g is outside [%g, %g]", comp->name, f.name,
                 x, f.min, f.max);
        return false;
    };
    switch (f.type) {
        case FieldType::kBool:
            if (v.type != TOML_BOOLEAN) return type_error();
            *(bool*)dst = v.u.boolean;
            return true;
        case FieldType::kI32:
        case FieldType::kU32:
        case FieldType::kU64: {
            if (v.type != TOML_INT64) return type_error();
            const int64_t x = v.u.int64;
            const bool fits = f.type == FieldType::kI32   ? (x >= INT32_MIN && x <= INT32_MAX)
                              : f.type == FieldType::kU32 ? (x >= 0 && x <= (int64_t)UINT32_MAX)
                                                          : x >= 0;
            if (!fits) {
                diag_add(diags, kErrSceneRange, file, v.lineno, v.colno, "%s.%s = %lld does not fit %s", comp->name,
                         f.name, (long long)x, tn);
                return false;
            }
            if (!in_range(f, (double)x)) return range_error((double)x);
            if (f.type == FieldType::kI32) *(int32_t*)dst = (int32_t)x;
            else if (f.type == FieldType::kU32) *(uint32_t*)dst = (uint32_t)x;
            else *(uint64_t*)dst = (uint64_t)x;
            return true;
        }
        case FieldType::kF32: {
            if (!is_number(v)) return type_error();
            const double x = number(v);
            if (!isfinite(x)) return type_error();
            if (!in_range(f, x)) return range_error(x);
            *(float*)dst = (float)x;
            return true;
        }
        case FieldType::kVec2:
        case FieldType::kVec3:
        case FieldType::kVec4: {
            const int n = f.type == FieldType::kVec2 ? 2 : (f.type == FieldType::kVec3 ? 3 : 4);
            if (v.type != TOML_ARRAY || v.u.arr.size != n) {
                diag_add(diags, kErrSceneType, file, v.lineno, v.colno, "%s.%s expects %s (an array of %d numbers)",
                         comp->name, f.name, tn, n);
                return false;
            }
            float tmp[4];
            for (int i = 0; i < n; ++i) {
                toml_datum_t e = v.u.arr.elem[i];
                if (is_token(e) && !resolve_token(v.u.arr.elem[i], false, theme, comp, f, nullptr, &e, file, diags)) {
                    return false;
                }
                if (!is_number(e)) return type_error();
                tmp[i] = (float)number(e);
            }
            memcpy(dst, tmp, sizeof(float) * (size_t)n);
            return true;
        }
        case FieldType::kString:
            if (v.type != TOML_STRING) return type_error();
            if ((uint32_t)v.u.str.len >= reflect::kStringCapacity) {
                diag_add(diags, kErrSceneRange, file, v.lineno, v.colno, "%s.%s is %d bytes (max %u)", comp->name,
                         f.name, v.u.str.len, reflect::kStringCapacity - 1);
                return false;
            }
            memcpy(dst, v.u.str.ptr, (size_t)v.u.str.len + 1);
            return true;
        case FieldType::kColor:
        case FieldType::kEnum: {
            if (v.type != TOML_STRING) return type_error();
            char err[300];
            if (!reflect::parse_text_value(f, v.u.str.ptr, dst, err, sizeof(err))) {
                diag_add(diags, kErrSceneRange, file, v.lineno, v.colno, "%s.%s %s", comp->name, f.name, err);
                return false;
            }
            return true;
        }
    }
    return type_error();
}

// A list of components being built for one entity or prefab.
struct ComponentSet {
    ComponentData items[kMaxComponentsPerEntity];
    uint32_t count = 0;

    ComponentData* find(const ComponentDesc* desc) {
        for (uint32_t i = 0; i < count; ++i) {
            if (items[i].desc == desc) return &items[i];
        }
        return nullptr;
    }
    // Returns the component, adding it from `init` (or the defaults) when missing.
    ComponentData* get_or_add(const ComponentDesc* desc, const void* init) {
        if (ComponentData* c = find(desc)) return c;
        if (count >= kMaxComponentsPerEntity) return nullptr;
        ComponentData& c = items[count++];
        c.desc = desc;
        c.data = malloc(desc->size);
        memcpy(c.data, init ? init : desc->defaults, desc->size);
        c.line = 0;
        c.field_lines = (int*)calloc(desc->field_count ? desc->field_count : 1, sizeof(int));
        return &c;
    }
    void release() {
        for (uint32_t i = 0; i < count; ++i) {
            free(items[i].data);
            free(items[i].field_lines);
        }
        count = 0;
    }
};

reflect::ComponentKind component_kind(DocKind kind) {
    return kind == DocKind::kScreen ? reflect::ComponentKind::kUi : reflect::ComponentKind::kScene;
}

// Applies one component table (`[Name]` or `[entity.Name]`) to a set.
void apply_component_table(const char* comp_name, const toml_datum_t& table, ComponentSet* set, DocKind kind,
                           const Theme* theme, const char* file, DiagList* diags) {
    const ComponentDesc* desc = reflect::find_component(comp_name);
    if (!desc) {
        const char* hint = reflect::suggest_component(comp_name);
        diag_add(diags, kErrSceneUnknownComponent, file, table.lineno, table.colno,
                 "unknown component '%s'%s%s%s (list them with `zaro schema`)", comp_name,
                 hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
        return;
    }
    if (desc->kind != component_kind(kind)) {
        diag_add(diags, kErrSceneUnknownComponent, file, table.lineno, table.colno,
                 desc->kind == reflect::ComponentKind::kUi
                     ? "'%s' is a UI widget; widgets go on [[node]]s of a [screen] file"
                     : "'%s' is a scene component; it goes on [[entity]]s of a [scene] file",
                 comp_name);
        return;
    }
    ComponentData* c = set->get_or_add(desc, nullptr);
    if (!c) {
        diag_add(diags, kErrSceneStructure, file, table.lineno, table.colno, "more than %u components",
                 kMaxComponentsPerEntity);
        return;
    }
    for (int32_t i = 0; i < table.u.tab.size; ++i) {
        const char* key = table.u.tab.key[i];
        const toml_datum_t& v = table.u.tab.value[i];
        const FieldDesc* f = reflect::find_field(desc, key);
        if (!f) {
            const char* hint = reflect::suggest_field(desc, key);
            diag_add(diags, kErrSceneUnknownField, file, v.lineno, v.colno, "%s has no field '%s'%s%s%s", desc->name, key,
                     hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
            continue;
        }
        if (assign_field(desc, *f, v, c->data, theme, file, diags)) c->field_lines[f - desc->fields] = v.lineno;
    }
    c->line = table.lineno;
}

bool copy_string(const toml_datum_t& v, char* out, size_t size, const char* what, const char* file, DiagList* diags) {
    if (v.type != TOML_STRING) {
        diag_add(diags, kErrSceneType, file, v.lineno, v.colno, "%s must be a string, got %s", what, kind_name(v.type));
        return false;
    }
    if ((size_t)v.u.str.len >= size) {
        diag_add(diags, kErrSceneRange, file, v.lineno, v.colno, "%s is longer than %zu bytes", what, size - 1);
        return false;
    }
    memcpy(out, v.u.str.ptr, (size_t)v.u.str.len + 1);
    return true;
}

// Tweens parsed from a file, waiting for their entity (or kept by a prefab).
struct TweenList {
    TweenSpec* items = nullptr;
    uint32_t count = 0, capacity = 0;
    void add(const TweenSpec& t) {
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 8;
            items = (TweenSpec*)realloc(items, sizeof(TweenSpec) * capacity);
        }
        items[count++] = t;
    }
    ~TweenList() { free(items); }
};

// Reads a tween's `to` or `from` for the field into lanes.
bool tween_value(const toml_datum_t& v, const FieldDesc* f, float lanes[4], const char* key, const char* file,
                 DiagList* diags) {
    const int n = f->type == FieldType::kVec2 ? 2 : (f->type == FieldType::kVec3 ? 3 : (f->type == FieldType::kVec4 ? 4 : 0));
    if (f->type == FieldType::kF32 && is_number(v)) {
        lanes[0] = (float)number(v);
        return true;
    }
    if (n && v.type == TOML_ARRAY && v.u.arr.size == n) {
        for (int i = 0; i < n; ++i) {
            if (!is_number(v.u.arr.elem[i])) break;
            lanes[i] = (float)number(v.u.arr.elem[i]);
            if (i == n - 1) return true;
        }
    }
    if (f->type == FieldType::kColor && v.type == TOML_STRING) {
        char err[300];
        uint32_t c = 0;
        if (!reflect::parse_text_value(*f, v.u.str.ptr, &c, err, sizeof(err))) {
            diag_add(diags, kErrTween, file, v.lineno, v.colno, "tween %s: %s", key, err);
            return false;
        }
        for (int i = 0; i < 4; ++i) lanes[i] = (float)((c >> (8 * i)) & 0xffu);
        return true;
    }
    diag_add(diags, kErrTween, file, v.lineno, v.colno, "tween %s must be %s for %s", key,
             f->type == FieldType::kF32 ? "a number" : (n ? "an array of numbers" : "a colour \"#rrggbb[aa]\""),
             reflect::type_name(f->type));
    return false;
}

// One [[tween]] table, checked against the components the entity or prefab has.
bool parse_tween(const toml_datum_t& t, const ComponentData* comps, uint32_t ncomps, TweenSpec* out, const char* file,
                 DiagList* diags) {
    if (t.type != TOML_TABLE) {
        diag_add(diags, kErrTween, file, t.lineno, t.colno, "tween must be a table ([[tween]])");
        return false;
    }
    const uint32_t before = diags->count;
    memset(out, 0, sizeof(*out));
    out->time = -1.0;
    const toml_datum_t comp = toml_get(t, "component");
    const toml_datum_t field = toml_get(t, "field");
    if (comp.type != TOML_STRING || field.type != TOML_STRING) {
        diag_add(diags, kErrTween, file, t.lineno, t.colno, "a tween needs component and field strings");
        return false;
    }
    const ComponentDesc* desc = reflect::find_component(comp.u.str.ptr);
    const ComponentData* have = nullptr;
    for (uint32_t i = 0; desc && i < ncomps; ++i) have = comps[i].desc == desc ? &comps[i] : have;
    if (!have) {
        const char* names[32];
        uint32_t n = 0;
        for (uint32_t i = 0; i < ncomps && n < 32; ++i) names[n++] = comps[i].desc->name;
        const char* hint = reflect::suggest_name(comp.u.str.ptr, names, n);
        diag_add(diags, kErrTween, file, comp.lineno, comp.colno, "the tween's component '%s' is not on it%s%s%s",
                 comp.u.str.ptr, hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
        return false;
    }
    const FieldDesc* f = reflect::find_field(desc, field.u.str.ptr);
    if (!f) {
        const char* hint = reflect::suggest_field(desc, field.u.str.ptr);
        diag_add(diags, kErrTween, file, field.lineno, field.colno, "%s has no field '%s'%s%s%s", desc->name,
                 field.u.str.ptr, hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
        return false;
    }
    if (const char* why = tween_target_error(desc, f)) {
        diag_add(diags, kErrTween, file, comp.lineno, comp.colno, "%s.%s %s", desc->name, f->name, why);
        return false;
    }
    out->component = desc;
    out->field = f;
    bool has_to = false;
    for (int32_t i = 0; i < t.u.tab.size; ++i) {
        const char* key = t.u.tab.key[i];
        const toml_datum_t& v = t.u.tab.value[i];
        if (strcmp(key, "component") == 0 || strcmp(key, "field") == 0) {
            continue;
        } else if (strcmp(key, "to") == 0) {
            has_to = tween_value(v, f, out->to, key, file, diags);
        } else if (strcmp(key, "from") == 0) {
            out->has_from = tween_value(v, f, out->from, key, file, diags);
        } else if (strcmp(key, "time") == 0 || strcmp(key, "delay") == 0) {
            const bool time = key[0] == 't';
            if (!is_number(v) || number(v) < 0.0 || (time && number(v) <= 0.0) || number(v) > 1e6) {
                diag_add(diags, kErrTween, file, v.lineno, v.colno, time ? "tween time must be seconds above 0"
                                                                          : "tween delay must be seconds, 0 or more");
            } else if (time) {
                out->time = number(v);
            } else {
                out->delay = number(v);
            }
        } else if (strcmp(key, "ease") == 0 || strcmp(key, "loop") == 0) {
            const bool is_ease = key[0] == 'e';
            const char* const* names = is_ease ? kEaseNames : kLoopNames;
            int found = -1;
            for (int k = 0; v.type == TOML_STRING && names[k]; ++k) found = strcmp(names[k], v.u.str.ptr) == 0 ? k : found;
            if (found < 0) {
                uint32_t n = 0;
                while (names[n]) ++n;
                const char* hint = v.type == TOML_STRING ? reflect::suggest_name(v.u.str.ptr, names, n) : nullptr;
                diag_add(diags, kErrTween, file, v.lineno, v.colno, "unknown tween %s '%s'%s%s%s (`zaro schema` lists them)",
                         key, v.type == TOML_STRING ? v.u.str.ptr : "?", hint ? " (did you mean '" : "", hint ? hint : "",
                         hint ? "'?)" : "");
            } else if (is_ease) {
                out->ease = (uint8_t)found;
            } else {
                out->loop = (uint8_t)found;
            }
        } else if (strcmp(key, "count") == 0) {
            if (v.type != TOML_INT64 || v.u.int64 < 0 || v.u.int64 > 1000000) {
                diag_add(diags, kErrTween, file, v.lineno, v.colno, "tween count must be a whole number, 0 or more");
            } else {
                out->count = (uint32_t)v.u.int64;
            }
        } else {
            diag_add(diags, kErrTween, file, v.lineno, v.colno,
                     "unexpected tween key '%s' (expected component, field, to, from, time, delay, ease, loop or count)",
                     key);
        }
    }
    if (!has_to && toml_get(t, "to").type == TOML_UNKNOWN) {
        diag_add(diags, kErrTween, file, t.lineno, t.colno, "a tween needs `to`");
    }
    if (out->time < 0.0 && toml_get(t, "time").type == TOML_UNKNOWN) {
        diag_add(diags, kErrTween, file, t.lineno, t.colno, "a tween needs `time` in seconds");
    }
    return diags->count == before;
}

// Every table of a `tween` array.
void parse_tweens(const toml_datum_t& v, const ComponentData* comps, uint32_t ncomps, TweenList* out, const char* file,
                  DiagList* diags) {
    if (v.type != TOML_ARRAY) {
        diag_add(diags, kErrTween, file, v.lineno, v.colno, "tween must be an array of tables ([[tween]])");
        return;
    }
    for (int32_t i = 0; i < v.u.arr.size; ++i) {
        TweenSpec spec;
        if (parse_tween(v.u.arr.elem[i], comps, ncomps, &spec, file, diags)) out->add(spec);
    }
}

struct Prefab {
    char path[kPathCapacity];
    char name[kNameCapacity];  // from the [prefab] table, empty if none
    TweenList tweens;
    bool ok;
    bool missing;  // the file could not be opened (already reported at the reference)
    ComponentSet set;
};

struct Loader {
    const char* scene_path;
    DocKind kind;
    const Theme* theme = nullptr;  // screens: tokens for "$name" values, in the screen and its prefabs
    DiagList* diags;
    Prefab* prefabs[kMaxPrefabs];
    uint32_t prefab_count = 0;
    // Tweens of the file's entities, by entity index, started once the scene is loaded.
    TweenList tweens;
    uint32_t* tween_entity = nullptr;

    const Prefab* prefab(const char* rel, const toml_datum_t& at) {
        char path[kPathCapacity];
        path_join_relative(scene_path, rel, path, sizeof(path));
        return prefab_at(path, at.lineno, at.colno);
    }

    // The prefab at a resolved path, loaded once; a missing file is reported at (line, col).
    const Prefab* prefab_at(const char* path, int line, int col) {
        for (uint32_t i = 0; i < prefab_count; ++i) {
            if (strcmp(prefabs[i]->path, path) == 0) return prefabs[i];
        }
        if (prefab_count >= kMaxPrefabs) return nullptr;
        Prefab* p = new Prefab();
        snprintf(p->path, sizeof(p->path), "%s", path);
        prefabs[prefab_count++] = p;
        const uint32_t errors_before = diags->count;
        Loaded l;
        const bool loaded = load_toml(path, &l, diags, kErrScenePrefab, scene_path, line, col);
        p->missing = !loaded && !l.text;
        if (loaded) {
            const toml_datum_t top = l.result.toptab;
            bool has_header = false;
            toml_datum_t tween_array{};
            for (int32_t i = 0; i < top.u.tab.size; ++i) {
                const char* key = top.u.tab.key[i];
                const toml_datum_t& v = top.u.tab.value[i];
                if (strcmp(key, "prefab") == 0) {
                    has_header = true;
                    const toml_datum_t name = toml_get(v, "name");
                    if (name.type != TOML_UNKNOWN) copy_string(name, p->name, sizeof(p->name), "prefab name", path, diags);
                    continue;
                }
                if (strcmp(key, "tween") == 0 && kind == DocKind::kScene) {
                    tween_array = v;
                    continue;
                }
                if (v.type != TOML_TABLE) {
                    diag_add(diags, kErrSceneStructure, path, v.lineno, v.colno,
                             "unexpected top-level key '%s' in a prefab (expected [prefab] or a component table)", key);
                    continue;
                }
                apply_component_table(key, v, &p->set, kind, theme, path, diags);
            }
            if (!has_header) diag_add(diags, kErrSceneStructure, path, 1, 1, "a prefab needs a [prefab] table");
            if (tween_array.type != TOML_UNKNOWN) {
                parse_tweens(tween_array, p->set.items, p->set.count, &p->tweens, path, diags);
            }
        }
        free_loaded(&l);
        p->ok = diags->count == errors_before;
        return p;
    }

    void add_tween(const TweenSpec& t, uint32_t entity) {
        tweens.add(t);
        tween_entity = (uint32_t*)realloc(tween_entity, sizeof(uint32_t) * tweens.capacity);
        tween_entity[tweens.count - 1] = entity;
    }

    ~Loader() {
        free(tween_entity);
        for (uint32_t i = 0; i < prefab_count; ++i) {
            prefabs[i]->set.release();
            delete prefabs[i];
        }
    }
};

void load_entity(Loader& loader, const toml_datum_t& t, Entity* e, uint32_t index, const char* file,
                 DiagList* diags) {
    const bool screen = loader.kind == DocKind::kScreen;
    const char* what = screen ? "node" : "entity";
    e->line = t.lineno;
    e->parent_index = -1;
    ComponentSet set;
    const Prefab* from_prefab = nullptr;
    toml_datum_t tweens{};
    const toml_datum_t prefab_v = toml_get(t, "prefab");
    if (prefab_v.type != TOML_UNKNOWN &&
        copy_string(prefab_v, e->prefab, sizeof(e->prefab), screen ? "node prefab" : "entity prefab", file, diags)) {
        const Prefab* p = loader.prefab(e->prefab, prefab_v);
        if (p && p->ok) {
            from_prefab = p;
            for (uint32_t i = 0; i < p->set.count; ++i) set.get_or_add(p->set.items[i].desc, p->set.items[i].data);
        } else if (p && !p->missing) {
            diag_add(diags, kErrScenePrefab, file, prefab_v.lineno, prefab_v.colno, "prefab '%s' has errors (see above)",
                     e->prefab);
        }
    }
    bool has_name = false;
    for (int32_t i = 0; i < t.u.tab.size; ++i) {
        const char* key = t.u.tab.key[i];
        const toml_datum_t& v = t.u.tab.value[i];
        if (strcmp(key, "name") == 0) {
            has_name = copy_string(v, e->name, sizeof(e->name), screen ? "node name" : "entity name", file, diags);
        } else if (strcmp(key, "prefab") == 0) {
            continue;
        } else if (strcmp(key, "parent") == 0) {
            copy_string(v, e->parent, sizeof(e->parent), screen ? "node parent" : "entity parent", file, diags);
        } else if (!screen && strcmp(key, "tween") == 0) {
            tweens = v;
        } else if (v.type == TOML_TABLE) {
            apply_component_table(key, v, &set, loader.kind, loader.theme, file, diags);
        } else {
            diag_add(diags, kErrSceneStructure, file, v.lineno, v.colno,
                     screen ? "unexpected node key '%s' (expected name, parent, prefab or a widget table)"
                            : "unexpected entity key '%s' (expected name, parent, prefab, tween or a component table)",
                     key);
        }
    }
    if (!has_name) diag_add(diags, kErrSceneStructure, file, t.lineno, t.colno, "%s needs a name", what);
    e->component_count = set.count;
    e->components = (ComponentData*)calloc(set.count ? set.count : 1, sizeof(ComponentData));
    memcpy(e->components, set.items, sizeof(ComponentData) * set.count);
    // The prefab's tweens, then the entity's own; a later one on the same field replaces an earlier.
    for (uint32_t i = 0; from_prefab && i < from_prefab->tweens.count; ++i) loader.add_tween(from_prefab->tweens.items[i], index);
    if (tweens.type != TOML_UNKNOWN) {
        TweenList own;
        parse_tweens(tweens, e->components, e->component_count, &own, file, diags);
        for (uint32_t i = 0; i < own.count; ++i) loader.add_tween(own.items[i], index);
    }
}

}  // namespace

namespace {

// Reads an array of strings into fixed slots. Returns the count, or adds diagnostics.
uint32_t copy_string_array(const toml_datum_t& v, char* out, uint32_t stride, uint32_t max, const char* what,
                           const char* file, DiagList* diags) {
    if (v.type != TOML_ARRAY) {
        diag_add(diags, kErrSceneType, file, v.lineno, v.colno, "%s must be an array of strings, got %s", what,
                 kind_name(v.type));
        return 0;
    }
    if ((uint32_t)v.u.arr.size > max) {
        diag_add(diags, kErrSceneRange, file, v.lineno, v.colno, "%s has more than %u entries", what, max);
        return 0;
    }
    uint32_t n = 0;
    for (int32_t i = 0; i < v.u.arr.size; ++i) {
        if (copy_string(v.u.arr.elem[i], out + (size_t)n * stride, stride, what, file, diags)) ++n;
    }
    return n;
}

void load_scene_header(const toml_datum_t& header, Scene* s, const char* path, DiagList* diags) {
    for (int32_t i = 0; i < header.u.tab.size; ++i) {
        const char* key = header.u.tab.key[i];
        const toml_datum_t& v = header.u.tab.value[i];
        char rel[kPathCapacity];
        if (strcmp(key, "name") == 0) {
            copy_string(v, s->name, sizeof(s->name), "scene name", path, diags);
        } else if (strcmp(key, "script") == 0) {
            if (copy_string(v, rel, sizeof(rel), "scene script", path, diags)) {
                path_join_relative(path, rel, s->script, sizeof(s->script));
            }
        } else if (strcmp(key, "ui") == 0) {
            char rels[kMaxSceneScreens][kPathCapacity];
            s->ui_line = v.lineno;
            s->ui_count = copy_string_array(v, rels[0], kPathCapacity, kMaxSceneScreens, "scene ui", path, diags);
            for (uint32_t k = 0; k < s->ui_count; ++k) path_join_relative(path, rels[k], s->ui[k], kPathCapacity);
        } else if (strcmp(key, "screens") == 0) {
            char rels[kMaxSceneScreens * 4][kPathCapacity];
            s->screens_line = v.lineno;
            s->screen_count =
                copy_string_array(v, rels[0], kPathCapacity, kMaxSceneScreens * 4, "scene screens", path, diags);
            for (uint32_t k = 0; k < s->screen_count; ++k) {
                path_join_relative(path, rels[k], s->screens[k], kPathCapacity);
            }
        } else if (strcmp(key, "strings") == 0) {
            s->strings_line = v.lineno;
            if (copy_string(v, rel, sizeof(rel) - 1, "scene strings", path, diags)) {
                path_join_relative(path, rel, s->strings, sizeof(s->strings) - 1);
                const size_t len = strlen(s->strings);
                if (len && s->strings[len - 1] != '/') memcpy(s->strings + len, "/", 2);
            }
        } else if (strcmp(key, "languages") == 0) {
            s->language_count = copy_string_array(v, s->languages[0], kLanguageCapacity, kMaxLanguages,
                                                  "scene languages", path, diags);
        } else if (strcmp(key, "prefabs") == 0) {
            char rels[kMaxSpawnPrefabs][kPathCapacity];
            s->prefabs_line = v.lineno;
            s->prefab_count = copy_string_array(v, rels[0], kPathCapacity, kMaxSpawnPrefabs, "scene prefabs", path, diags);
            s->prefabs = (SpawnPrefab*)calloc(s->prefab_count ? s->prefab_count : 1, sizeof(SpawnPrefab));
            for (uint32_t k = 0; k < s->prefab_count; ++k) {
                path_join_relative(path, rels[k], s->prefabs[k].path, kPathCapacity);
            }
        } else if (strcmp(key, "build_extensions") == 0) {
            char rels[kMaxBuildExtensions][kPathCapacity];
            s->build_extensions_line = v.lineno;
            s->build_extension_count = copy_string_array(v, rels[0], kPathCapacity, kMaxBuildExtensions,
                                                         "scene build_extensions", path, diags);
            for (uint32_t k = 0; k < s->build_extension_count; ++k) {
                path_join_relative(path, rels[k], s->build_extensions[k], kPathCapacity);
            }
        } else {
            diag_add(diags, kErrSceneStructure, path, v.lineno, v.colno,
                     "unexpected key '%s' in [scene] (expected name, script, ui, screens, strings, languages, "
                     "build_extensions or prefabs)", key);
        }
    }
    if (s->strings[0] && !s->language_count) {
        diag_add(diags, kErrSceneStructure, path, s->strings_line, 1, "strings needs languages = [\"..\", ...]");
    }
    if (!s->strings[0] && s->language_count) {
        diag_add(diags, kErrSceneStructure, path, header.lineno, header.colno, "languages needs strings = \"<dir>\"");
    }
}

void load_screen_header(const toml_datum_t& header, Scene* s, const char* path, DiagList* diags) {
    for (int32_t i = 0; i < header.u.tab.size; ++i) {
        const char* key = header.u.tab.key[i];
        const toml_datum_t& v = header.u.tab.value[i];
        bool* flag = strcmp(key, "navigable") == 0     ? &s->navigable
                     : strcmp(key, "dismissible") == 0 ? &s->dismissible
                     : strcmp(key, "skippable") == 0   ? &s->skippable
                                                       : nullptr;
        if (strcmp(key, "name") == 0) {
            copy_string(v, s->name, sizeof(s->name), "screen name", path, diags);
        } else if (flag) {
            if (v.type == TOML_BOOLEAN) *flag = v.u.boolean;
            else diag_add(diags, kErrSceneType, path, v.lineno, v.colno, "%s must be a boolean", key);
        } else if (strcmp(key, "transition") == 0) {
            static const char* const kTransitions[] = {"none", "fade", "pop", nullptr};
            const char* hint = v.type == TOML_STRING ? reflect::suggest_name(v.u.str.ptr, kTransitions, 3) : nullptr;
            bool found = false;
            for (uint32_t k = 0; v.type == TOML_STRING && kTransitions[k]; ++k) {
                if (strcmp(v.u.str.ptr, kTransitions[k]) == 0) {
                    s->transition = k;
                    found = true;
                }
            }
            if (!found) {
                diag_add(diags, kErrSceneRange, path, v.lineno, v.colno,
                         "transition expects one of none, fade, pop%s%s%s", hint ? " (did you mean '" : "",
                         hint ? hint : "", hint ? "'?)" : "");
            }
        } else if (strcmp(key, "theme") == 0) {
            char rel[kPathCapacity];
            s->theme_line = v.lineno;
            if (copy_string(v, rel, sizeof(rel), "screen theme", path, diags)) {
                path_join_relative(path, rel, s->theme, sizeof(s->theme));
            }
        } else if (strcmp(key, "transition_time") == 0) {
            if (is_number(v) && number(v) >= 0.0 && number(v) <= 10.0) s->transition_time = (float)number(v);
            else diag_add(diags, kErrSceneRange, path, v.lineno, v.colno, "transition_time must be 0 to 10 seconds");
        } else if (strcmp(key, "reference") == 0) {
            const bool ok = v.type == TOML_ARRAY && v.u.arr.size == 2 && is_number(v.u.arr.elem[0]) &&
                            is_number(v.u.arr.elem[1]) && number(v.u.arr.elem[0]) >= 1.0 &&
                            number(v.u.arr.elem[1]) >= 1.0;
            if (ok) {
                s->reference[0] = (float)number(v.u.arr.elem[0]);
                s->reference[1] = (float)number(v.u.arr.elem[1]);
            } else {
                diag_add(diags, kErrSceneType, path, v.lineno, v.colno,
                         "reference must be [width, height] with both at least 1");
            }
        } else {
            diag_add(diags, kErrSceneStructure, path, v.lineno, v.colno,
                     "unexpected key '%s' in [screen] (expected name, theme, reference, navigable, dismissible, "
                     "transition, transition_time or skippable)",
                     key);
        }
    }
}

// Links every node or entity to its parent, which must come earlier in the file.
void resolve_parents(Scene* s, const toml_datum_t& nodes, DiagList* diags) {
    const char* what = s->kind == DocKind::kScreen ? "node" : "entity";
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        Entity& e = s->entities[i];
        if (!e.parent[0]) continue;
        for (uint32_t j = 0; j < i; ++j) {
            if (strcmp(s->entities[j].name, e.parent) == 0) e.parent_index = (int)j;
        }
        if (e.parent_index >= 0) continue;
        const toml_datum_t at = toml_get(nodes.u.arr.elem[i], "parent");
        bool later = false;
        for (uint32_t j = i; j < s->entity_count; ++j) later = later || strcmp(s->entities[j].name, e.parent) == 0;
        if (later) {
            diag_add(diags, kErrSceneParent, s->path, at.lineno, at.colno,
                     "parent '%s' must come before %s '%s' in the file", e.parent, what, e.name);
            continue;
        }
        const char* names[kMaxEntities];
        for (uint32_t j = 0; j < i; ++j) names[j] = s->entities[j].name;
        const char* hint = reflect::suggest_name(e.parent, names, i);
        diag_add(diags, kErrSceneParent, s->path, at.lineno, at.colno, "unknown parent %s '%s'%s%s%s", what, e.parent,
                 hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
    }
}

// Loads [scene] prefabs and keeps a copy of each one's components for spawning (scene/world.h).
void load_spawn_prefabs(Loader& loader, Scene* s, DiagList* diags) {
    for (uint32_t i = 0; i < s->prefab_count; ++i) {
        SpawnPrefab& sp = s->prefabs[i];
        const Prefab* p = loader.prefab_at(sp.path, s->prefabs_line, 1);
        if (!p || !p->ok) {
            if (p && !p->missing) {
                diag_add(diags, kErrScenePrefab, s->path, s->prefabs_line, 1, "prefab '%s' has errors (see above)",
                         sp.path);
            }
            continue;
        }
        if (!p->name[0]) {
            diag_add(diags, kErrSpawnPrefab, sp.path, 1, 1,
                     "a prefab in [scene] prefabs needs a name in its [prefab] table; scripts spawn it by that name");
            continue;
        }
        for (uint32_t j = 0; j < i; ++j) {
            if (strcmp(s->prefabs[j].name, p->name) == 0) {
                diag_add(diags, kErrSpawnPrefab, s->path, s->prefabs_line, 1,
                         "prefabs '%s' and '%s' are both named '%s'", s->prefabs[j].path, sp.path, p->name);
            }
        }
        snprintf(sp.name, sizeof(sp.name), "%s", p->name);
        if (p->tweens.count) {
            sp.tweens = (TweenSpec*)malloc(sizeof(TweenSpec) * p->tweens.count);
            memcpy(sp.tweens, p->tweens.items, sizeof(TweenSpec) * p->tweens.count);
            sp.tween_count = p->tweens.count;
        }
        sp.component_count = p->set.count;
        sp.components = (ComponentData*)calloc(p->set.count ? p->set.count : 1, sizeof(ComponentData));
        for (uint32_t c = 0; c < p->set.count; ++c) {
            const ComponentData& from = p->set.items[c];
            ComponentData& to = sp.components[c];
            to.desc = from.desc;
            to.data = malloc(from.desc->size);
            memcpy(to.data, from.data, from.desc->size);
            to.line = 0;
            to.field_lines = nullptr;
        }
    }
}

enum class Want : uint8_t { kAny, kScene, kScreen };

Scene* load(const char* path, Want expect, DiagList* diags) {
    const uint32_t errors_before = diags->count;
    Loaded l;
    if (!load_toml(path, &l, diags, kErrSceneSyntax, nullptr, 0, 0)) {
        free_loaded(&l);
        return nullptr;
    }
    Scene* s = (Scene*)calloc(1, sizeof(Scene));
    snprintf(s->path, sizeof(s->path), "%s", path);
    s->reference[0] = 1280.0f;
    s->reference[1] = 720.0f;
    s->dismissible = true;
    s->skippable = true;
    s->transition_time = 0.15f;
    const toml_datum_t top = l.result.toptab;
    const toml_datum_t scene_header = toml_get(top, "scene");
    const toml_datum_t screen_header = toml_get(top, "screen");
    const bool screen = screen_header.type != TOML_UNKNOWN && scene_header.type == TOML_UNKNOWN;
    s->kind = screen ? DocKind::kScreen : DocKind::kScene;
    const char* header_key = screen ? "screen" : "scene";
    const char* items_key = screen ? "node" : "entity";
    const toml_datum_t header = screen ? screen_header : scene_header;
    Loader loader;
    loader.scene_path = path;
    loader.kind = s->kind;
    loader.diags = diags;

    if ((expect == Want::kScene && screen) || (expect == Want::kScreen && !screen)) {
        diag_add(diags, kErrSceneStructure, path, 1, 1, "expected a %s file with a [%s] table, got a %s file",
                 expect == Want::kScene ? "scene" : "screen", expect == Want::kScene ? "scene" : "screen",
                 screen ? "screen" : "scene");
    } else if (header.type != TOML_TABLE) {
        diag_add(diags, kErrSceneStructure, path, 1, 1, "a %s needs a [%s] table with a name",
                 expect == Want::kScreen ? "screen" : "scene", expect == Want::kScreen ? "screen" : "scene");
    } else {
        if (screen) load_screen_header(header, s, path, diags);
        else load_scene_header(header, s, path, diags);
        if (!s->name[0]) {
            diag_add(diags, kErrSceneStructure, path, header.lineno, header.colno, "[%s] needs a name", header_key);
        }
    }

    for (int32_t i = 0; i < top.u.tab.size; ++i) {
        const char* key = top.u.tab.key[i];
        if (strcmp(key, header_key) != 0 && strcmp(key, items_key) != 0) {
            const toml_datum_t& v = top.u.tab.value[i];
            diag_add(diags, kErrSceneStructure, path, v.lineno, v.colno,
                     "unexpected top-level key '%s' (expected [%s] or [[%s]])", key, header_key, items_key);
        }
    }

    // The screen's theme resolves "$name" values in its nodes and prefabs; it is not kept.
    Theme* theme = nullptr;
    if (s->theme[0]) {
        const uint32_t before = diags->count;
        theme = load_theme(s->theme, diags);
        if (!theme && diags->count == before + 1 && diags->items[before].line == 0 &&
            strcmp(diags->items[before].file, s->theme) == 0) {
            // A missing theme file is reported where the screen names it.
            diags->count = before;
            diag_add(diags, kErrSceneStructure, path, s->theme_line, 1, "cannot open theme '%s'", s->theme);
        }
    }
    loader.theme = theme;
    const toml_datum_t ents = toml_get(top, items_key);
    if (ents.type == TOML_ARRAY) {
        const uint32_t n = (uint32_t)ents.u.arr.size < kMaxEntities ? (uint32_t)ents.u.arr.size : kMaxEntities;
        s->entities = (Entity*)calloc(n ? n : 1, sizeof(Entity));
        for (uint32_t i = 0; i < n; ++i) {
            const toml_datum_t& t = ents.u.arr.elem[i];
            if (t.type != TOML_TABLE) {
                diag_add(diags, kErrSceneStructure, path, t.lineno, t.colno, "%s must be a table ([[%s]])", items_key,
                         items_key);
                continue;
            }
            Entity* e = &s->entities[s->entity_count++];
            load_entity(loader, t, e, s->entity_count - 1, path, diags);
            for (uint32_t j = 0; j + 1 < s->entity_count; ++j) {
                if (e->name[0] && strcmp(s->entities[j].name, e->name) == 0) {
                    diag_add(diags, kErrSceneDuplicate, path, t.lineno, t.colno, "%s name '%s' is already used on line %d",
                             items_key, e->name, s->entities[j].line);
                }
            }
        }
        // Parents are looked up by position in the array, so every element must have become a node.
        if (s->entity_count == n) resolve_parents(s, ents, diags);
    } else if (ents.type != TOML_UNKNOWN) {
        diag_add(diags, kErrSceneStructure, path, ents.lineno, ents.colno, "%s must be an array of tables ([[%s]])",
                 items_key, items_key);
    }

    for (uint32_t i = 0; i < s->entity_count; ++i) s->entities[i].id = i + 1;
    for (uint32_t i = 0; !screen && i < s->entity_count; ++i) {
        const int p = s->entities[i].parent_index;
        s->entities[i].parent_id = p >= 0 ? s->entities[p].id : 0;
    }
    s->next_id = s->entity_count + 1;
    s->entity_capacity = s->entities ? (s->entity_count ? s->entity_count : 1) : 0;
    load_spawn_prefabs(loader, s, diags);
    for (uint32_t i = 0; i < loader.tweens.count; ++i) {
        const uint32_t e = loader.tween_entity[i];
        if (e < s->entity_count) start_tween(s, loader.tweens.items[i], s->entities[e].id, nullptr, nullptr, 0, 0);
    }

    // Build extensions are checked with the scene, so `zaro check` reports them (D-026).
    for (uint32_t i = 0; i < s->build_extension_count; ++i) {
        const uint32_t before = diags->count;
        BuildExtension* x = load_build_extension(s->build_extensions[i], diags);
        if (!x && diags->count == before + 1 && diags->items[before].line == 0 &&
            strcmp(diags->items[before].file, s->build_extensions[i]) == 0 &&
            strncmp(diags->items[before].message, "cannot open", 11) == 0) {
            // A missing file is reported where the scene names it.
            diags->count = before;
            diag_add(diags, kErrSceneStructure, path, s->build_extensions_line, 1, "cannot open build extension '%s'",
                     s->build_extensions[i]);
        }
        free_build_extension(x);
    }

    free_theme(theme);
    free_loaded(&l);
    if (diags->count != errors_before) {
        free_scene(s);
        return nullptr;
    }
    return s;
}

}  // namespace

Scene* load_document(const char* path, DiagList* diags) { return load(path, Want::kAny, diags); }
Scene* load_scene(const char* path, DiagList* diags) { return load(path, Want::kScene, diags); }
Scene* load_screen(const char* path, DiagList* diags) { return load(path, Want::kScreen, diags); }

void free_scene(Scene* s) {
    if (!s) return;
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        for (uint32_t j = 0; j < s->entities[i].component_count; ++j) {
            free(s->entities[i].components[j].data);
            free(s->entities[i].components[j].field_lines);
        }
        free(s->entities[i].components);
    }
    free(s->entities);
    for (uint32_t i = 0; s->prefabs && i < s->prefab_count; ++i) {
        for (uint32_t j = 0; j < s->prefabs[i].component_count; ++j) free(s->prefabs[i].components[j].data);
        free(s->prefabs[i].components);
        free(s->prefabs[i].tweens);
    }
    free(s->prefabs);
    free_tweens(s);
    free(s);
}

const Entity* find_entity(const Scene* s, const char* name) {
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        if (!s->entities[i].destroyed && strcmp(s->entities[i].name, name) == 0) return &s->entities[i];
    }
    return nullptr;
}

const void* get_component(const Entity* e, const ComponentDesc* desc) {
    for (uint32_t i = 0; i < e->component_count; ++i) {
        if (e->components[i].desc == desc) return e->components[i].data;
    }
    return nullptr;
}

const ComponentData* find_component_data(const Entity* e, const ComponentDesc* desc) {
    for (uint32_t i = 0; i < e->component_count; ++i) {
        if (e->components[i].desc == desc) return &e->components[i];
    }
    return nullptr;
}

const void* first_component(const Scene* s, const ComponentDesc* desc, const Entity** entity) {
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        if (const void* c = get_component(&s->entities[i], desc)) {
            if (entity) *entity = &s->entities[i];
            return c;
        }
    }
    return nullptr;
}

void* first_component_mut(Scene* s, const ComponentDesc* desc) {
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        for (uint32_t j = 0; j < s->entities[i].component_count; ++j) {
            if (s->entities[i].components[j].desc == desc) return s->entities[i].components[j].data;
        }
    }
    return nullptr;
}

}  // namespace zaro::scene
