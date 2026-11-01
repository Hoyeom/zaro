#include "scene/scene.h"

#include "core/path.h"

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

// Writes one TOML value into a component field. Returns false (with a diagnostic) on mismatch.
bool assign_field(const ComponentDesc* comp, const FieldDesc& f, const toml_datum_t& v, void* data,
                  const char* file, DiagList* diags) {
    uint8_t* dst = (uint8_t*)data + f.offset;
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
                if (!is_number(v.u.arr.elem[i])) return type_error();
                tmp[i] = (float)number(v.u.arr.elem[i]);
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
                           const char* file, DiagList* diags) {
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
        if (assign_field(desc, *f, v, c->data, file, diags)) c->field_lines[f - desc->fields] = v.lineno;
    }
    c->line = table.lineno;
}

struct Prefab {
    char path[kPathCapacity];
    bool ok;
    bool missing;  // the file could not be opened (already reported at the reference)
    ComponentSet set;
};

struct Loader {
    const char* scene_path;
    DocKind kind;
    DiagList* diags;
    Prefab* prefabs[kMaxPrefabs];
    uint32_t prefab_count = 0;

    const Prefab* prefab(const char* rel, const toml_datum_t& at) {
        char path[kPathCapacity];
        path_join_relative(scene_path, rel, path, sizeof(path));
        for (uint32_t i = 0; i < prefab_count; ++i) {
            if (strcmp(prefabs[i]->path, path) == 0) return prefabs[i];
        }
        if (prefab_count >= kMaxPrefabs) return nullptr;
        Prefab* p = new Prefab();
        snprintf(p->path, sizeof(p->path), "%s", path);
        prefabs[prefab_count++] = p;
        const uint32_t errors_before = diags->count;
        Loaded l;
        const bool loaded = load_toml(path, &l, diags, kErrScenePrefab, scene_path, at.lineno, at.colno);
        p->missing = !loaded && !l.text;
        if (loaded) {
            const toml_datum_t top = l.result.toptab;
            bool has_header = false;
            for (int32_t i = 0; i < top.u.tab.size; ++i) {
                const char* key = top.u.tab.key[i];
                const toml_datum_t& v = top.u.tab.value[i];
                if (strcmp(key, "prefab") == 0) {
                    has_header = true;
                    continue;
                }
                if (v.type != TOML_TABLE) {
                    diag_add(diags, kErrSceneStructure, path, v.lineno, v.colno,
                             "unexpected top-level key '%s' in a prefab (expected [prefab] or a component table)", key);
                    continue;
                }
                apply_component_table(key, v, &p->set, kind, path, diags);
            }
            if (!has_header) diag_add(diags, kErrSceneStructure, path, 1, 1, "a prefab needs a [prefab] table");
        }
        free_loaded(&l);
        p->ok = diags->count == errors_before;
        return p;
    }

    ~Loader() {
        for (uint32_t i = 0; i < prefab_count; ++i) {
            prefabs[i]->set.release();
            delete prefabs[i];
        }
    }
};

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

void load_entity(Loader& loader, const toml_datum_t& t, Entity* e, const char* file, DiagList* diags) {
    const bool screen = loader.kind == DocKind::kScreen;
    const char* what = screen ? "node" : "entity";
    e->line = t.lineno;
    e->parent_index = -1;
    ComponentSet set;
    const toml_datum_t prefab_v = toml_get(t, "prefab");
    if (prefab_v.type != TOML_UNKNOWN &&
        copy_string(prefab_v, e->prefab, sizeof(e->prefab), screen ? "node prefab" : "entity prefab", file, diags)) {
        const Prefab* p = loader.prefab(e->prefab, prefab_v);
        if (p && p->ok) {
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
        } else if (screen && strcmp(key, "parent") == 0) {
            copy_string(v, e->parent, sizeof(e->parent), "node parent", file, diags);
        } else if (v.type == TOML_TABLE) {
            apply_component_table(key, v, &set, loader.kind, file, diags);
        } else {
            diag_add(diags, kErrSceneStructure, file, v.lineno, v.colno,
                     screen ? "unexpected node key '%s' (expected name, parent, prefab or a widget table)"
                            : "unexpected entity key '%s' (expected name, prefab or a component table)",
                     key);
        }
    }
    if (!has_name) diag_add(diags, kErrSceneStructure, file, t.lineno, t.colno, "%s needs a name", what);
    e->component_count = set.count;
    e->components = (ComponentData*)calloc(set.count ? set.count : 1, sizeof(ComponentData));
    memcpy(e->components, set.items, sizeof(ComponentData) * set.count);
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
        } else {
            diag_add(diags, kErrSceneStructure, path, v.lineno, v.colno,
                     "unexpected key '%s' in [scene] (expected name, script, ui, strings or languages)", key);
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
        if (strcmp(key, "name") == 0) {
            copy_string(v, s->name, sizeof(s->name), "screen name", path, diags);
        } else if (strcmp(key, "navigable") == 0) {
            if (v.type == TOML_BOOLEAN) s->navigable = v.u.boolean;
            else diag_add(diags, kErrSceneType, path, v.lineno, v.colno, "navigable must be a boolean");
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
                     "unexpected key '%s' in [screen] (expected name, reference or navigable)", key);
        }
    }
}

// Links every node to its parent, which must be an earlier node of the screen.
void resolve_parents(Scene* s, const toml_datum_t& nodes, DiagList* diags) {
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
                     "parent '%s' must come before node '%s' in the file", e.parent, e.name);
            continue;
        }
        const char* names[kMaxEntities];
        for (uint32_t j = 0; j < i; ++j) names[j] = s->entities[j].name;
        const char* hint = reflect::suggest_name(e.parent, names, i);
        diag_add(diags, kErrSceneParent, s->path, at.lineno, at.colno, "unknown parent node '%s'%s%s%s", e.parent,
                 hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
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
            load_entity(loader, t, e, path, diags);
            for (uint32_t j = 0; j + 1 < s->entity_count; ++j) {
                if (e->name[0] && strcmp(s->entities[j].name, e->name) == 0) {
                    diag_add(diags, kErrSceneDuplicate, path, t.lineno, t.colno, "%s name '%s' is already used on line %d",
                             items_key, e->name, s->entities[j].line);
                }
            }
        }
        // Parents are looked up by position in the array, so every element must have become a node.
        if (screen && s->entity_count == n) resolve_parents(s, ents, diags);
    } else if (ents.type != TOML_UNKNOWN) {
        diag_add(diags, kErrSceneStructure, path, ents.lineno, ents.colno, "%s must be an array of tables ([[%s]])",
                 items_key, items_key);
    }

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
    free(s);
}

const Entity* find_entity(const Scene* s, const char* name) {
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        if (strcmp(s->entities[i].name, name) == 0) return &s->entities[i];
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
