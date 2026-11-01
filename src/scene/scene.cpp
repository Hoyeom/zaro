#include "scene/scene.h"

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

void resolve_relative(const char* base_file, const char* rel, char* out, size_t size) {
    const char* slash = strrchr(base_file, '/');
    const char* bslash = strrchr(base_file, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
    if (!slash || rel[0] == '/' || (rel[0] && rel[1] == ':')) {
        snprintf(out, size, "%s", rel);
        return;
    }
    snprintf(out, size, "%.*s/%s", (int)(slash - base_file), base_file, rel);
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
        return &c;
    }
    void release() {
        for (uint32_t i = 0; i < count; ++i) free(items[i].data);
        count = 0;
    }
};

// Applies one component table (`[Name]` or `[entity.Name]`) to a set.
void apply_component_table(const char* comp_name, const toml_datum_t& table, ComponentSet* set, const char* file,
                           DiagList* diags) {
    const ComponentDesc* desc = reflect::find_component(comp_name);
    if (!desc) {
        const char* hint = reflect::suggest_component(comp_name);
        diag_add(diags, kErrSceneUnknownComponent, file, table.lineno, table.colno,
                 "unknown component '%s'%s%s%s (list them with `zaro schema`)", comp_name,
                 hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
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
        assign_field(desc, *f, v, c->data, file, diags);
    }
}

struct Prefab {
    char path[kPathCapacity];
    bool ok;
    bool missing;  // the file could not be opened (already reported at the reference)
    ComponentSet set;
};

struct Loader {
    const char* scene_path;
    DiagList* diags;
    Prefab* prefabs[kMaxPrefabs];
    uint32_t prefab_count = 0;

    const Prefab* prefab(const char* rel, const toml_datum_t& at) {
        char path[kPathCapacity];
        resolve_relative(scene_path, rel, path, sizeof(path));
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
                apply_component_table(key, v, &p->set, path, diags);
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
    e->line = t.lineno;
    ComponentSet set;
    const toml_datum_t prefab_v = toml_get(t, "prefab");
    if (prefab_v.type != TOML_UNKNOWN &&
        copy_string(prefab_v, e->prefab, sizeof(e->prefab), "entity prefab", file, diags)) {
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
            has_name = copy_string(v, e->name, sizeof(e->name), "entity name", file, diags);
        } else if (strcmp(key, "prefab") == 0) {
            continue;
        } else if (v.type == TOML_TABLE) {
            apply_component_table(key, v, &set, file, diags);
        } else {
            diag_add(diags, kErrSceneStructure, file, v.lineno, v.colno,
                     "unexpected entity key '%s' (expected name, prefab or a component table)", key);
        }
    }
    if (!has_name) diag_add(diags, kErrSceneStructure, file, t.lineno, t.colno, "entity needs a name");
    e->component_count = set.count;
    e->components = (ComponentData*)calloc(set.count ? set.count : 1, sizeof(ComponentData));
    memcpy(e->components, set.items, sizeof(ComponentData) * set.count);
}

}  // namespace

Scene* load_scene(const char* path, DiagList* diags) {
    const uint32_t errors_before = diags->count;
    Loaded l;
    if (!load_toml(path, &l, diags, kErrSceneSyntax, nullptr, 0, 0)) {
        free_loaded(&l);
        return nullptr;
    }
    Scene* s = (Scene*)calloc(1, sizeof(Scene));
    snprintf(s->path, sizeof(s->path), "%s", path);
    Loader loader;
    loader.scene_path = path;
    loader.diags = diags;

    const toml_datum_t top = l.result.toptab;
    const toml_datum_t header = toml_get(top, "scene");
    if (header.type != TOML_TABLE) {
        diag_add(diags, kErrSceneStructure, path, 1, 1, "a scene needs a [scene] table with a name");
    } else {
        for (int32_t i = 0; i < header.u.tab.size; ++i) {
            const char* key = header.u.tab.key[i];
            const toml_datum_t& v = header.u.tab.value[i];
            if (strcmp(key, "name") == 0) {
                copy_string(v, s->name, sizeof(s->name), "scene name", path, diags);
            } else if (strcmp(key, "script") == 0) {
                char rel[kPathCapacity];
                if (copy_string(v, rel, sizeof(rel), "scene script", path, diags)) {
                    resolve_relative(path, rel, s->script, sizeof(s->script));
                }
            } else {
                diag_add(diags, kErrSceneStructure, path, v.lineno, v.colno,
                         "unexpected key '%s' in [scene] (expected name or script)", key);
            }
        }
        if (!s->name[0]) diag_add(diags, kErrSceneStructure, path, header.lineno, header.colno, "[scene] needs a name");
    }

    for (int32_t i = 0; i < top.u.tab.size; ++i) {
        const char* key = top.u.tab.key[i];
        if (strcmp(key, "scene") != 0 && strcmp(key, "entity") != 0) {
            const toml_datum_t& v = top.u.tab.value[i];
            diag_add(diags, kErrSceneStructure, path, v.lineno, v.colno,
                     "unexpected top-level key '%s' (expected [scene] or [[entity]])", key);
        }
    }

    const toml_datum_t ents = toml_get(top, "entity");
    if (ents.type == TOML_ARRAY) {
        const uint32_t n = (uint32_t)ents.u.arr.size < kMaxEntities ? (uint32_t)ents.u.arr.size : kMaxEntities;
        s->entities = (Entity*)calloc(n ? n : 1, sizeof(Entity));
        for (uint32_t i = 0; i < n; ++i) {
            const toml_datum_t& t = ents.u.arr.elem[i];
            if (t.type != TOML_TABLE) {
                diag_add(diags, kErrSceneStructure, path, t.lineno, t.colno, "entity must be a table ([[entity]])");
                continue;
            }
            Entity* e = &s->entities[s->entity_count++];
            load_entity(loader, t, e, path, diags);
            for (uint32_t j = 0; j + 1 < s->entity_count; ++j) {
                if (e->name[0] && strcmp(s->entities[j].name, e->name) == 0) {
                    diag_add(diags, kErrSceneDuplicate, path, t.lineno, t.colno,
                             "entity name '%s' is already used on line %d", e->name, s->entities[j].line);
                }
            }
        }
    } else if (ents.type != TOML_UNKNOWN) {
        diag_add(diags, kErrSceneStructure, path, ents.lineno, ents.colno, "entity must be an array of tables ([[entity]])");
    }

    free_loaded(&l);
    if (diags->count != errors_before) {
        free_scene(s);
        return nullptr;
    }
    return s;
}

void free_scene(Scene* s) {
    if (!s) return;
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        for (uint32_t j = 0; j < s->entities[i].component_count; ++j) free(s->entities[i].components[j].data);
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
