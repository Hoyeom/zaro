#include "scene/world.h"

#include "core/hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

using reflect::ComponentDesc;
using reflect::FieldDesc;
using reflect::FieldType;

int find_spawn_prefab(const Scene* s, const char* name) {
    for (uint32_t i = 0; i < s->prefab_count; ++i) {
        if (strcmp(s->prefabs[i].name, name) == 0) return (int)i;
    }
    return -1;
}

const char* suggest_spawn_prefab(const Scene* s, const char* name) {
    const char* names[kMaxSpawnPrefabs];
    for (uint32_t i = 0; i < s->prefab_count; ++i) names[i] = s->prefabs[i].name;
    return reflect::suggest_name(name, names, s->prefab_count);
}

Entity* entity_by_id(Scene* s, uint32_t id) {
    // Ids ascend with the index, so a binary search finds one.
    uint32_t lo = 0, hi = s->entity_count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (s->entities[mid].id < id) lo = mid + 1;
        else hi = mid;
    }
    if (lo < s->entity_count && s->entities[lo].id == id && !s->entities[lo].destroyed) return &s->entities[lo];
    return nullptr;
}

Entity* entity_by_name(Scene* s, const char* name) {
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        if (!s->entities[i].destroyed && strcmp(s->entities[i].name, name) == 0) return &s->entities[i];
    }
    return nullptr;
}

const char* suggest_entity(const Scene* s, const char* name) {
    constexpr uint32_t kMax = 4096;  // the file's entities come first, so they are always considered
    const char* names[kMax];
    uint32_t n = 0;
    for (uint32_t i = 0; i < s->entity_count && n < kMax; ++i) {
        if (!s->entities[i].destroyed) names[n++] = s->entities[i].name;
    }
    return reflect::suggest_name(name, names, n);
}

Entity* spawn_entity(Scene* s, int prefab, const char* name, char* err, uint32_t err_size) {
    uint32_t live = 0;
    for (uint32_t i = 0; i < s->entity_count; ++i) live += s->entities[i].destroyed ? 0 : 1;
    if (live >= kMaxLiveEntities) {
        snprintf(err, err_size, "cannot spawn: the scene already has %u entities", kMaxLiveEntities);
        return nullptr;
    }
    char given[kNameCapacity];
    if (name) {
        if (!name[0] || strlen(name) >= kNameCapacity) {
            snprintf(err, err_size, "entity names are 1 to %u bytes", kNameCapacity - 1);
            return nullptr;
        }
        if (entity_by_name(s, name)) {
            snprintf(err, err_size, "an entity named '%s' already exists", name);
            return nullptr;
        }
        snprintf(given, sizeof(given), "%s", name);
    } else {
        snprintf(given, sizeof(given), "%s#%u", prefab >= 0 ? s->prefabs[prefab].name : "entity", s->next_id);
    }
    if (s->entity_count == s->entity_capacity) {
        const uint32_t cap = s->entity_capacity ? s->entity_capacity * 2 : 16;
        s->entities = (Entity*)realloc(s->entities, sizeof(Entity) * cap);
        s->entity_capacity = cap;
    }
    Entity* e = &s->entities[s->entity_count++];
    memset(e, 0, sizeof(*e));
    memcpy(e->name, given, sizeof(given));
    e->parent_index = -1;
    e->id = s->next_id++;
    if (prefab >= 0) {
        const SpawnPrefab& p = s->prefabs[prefab];
        snprintf(e->prefab, sizeof(e->prefab), "%s", p.path);
        e->component_count = p.component_count;
        e->components = (ComponentData*)calloc(p.component_count ? p.component_count : 1, sizeof(ComponentData));
        for (uint32_t c = 0; c < p.component_count; ++c) {
            ComponentData& to = e->components[c];
            to.desc = p.components[c].desc;
            to.data = malloc(to.desc->size);
            memcpy(to.data, p.components[c].data, to.desc->size);
        }
    } else {
        e->components = (ComponentData*)calloc(1, sizeof(ComponentData));
    }
    s->generation++;
    return e;
}

void* add_component(Entity* e, const ComponentDesc* desc) {
    for (uint32_t i = 0; i < e->component_count; ++i) {
        if (e->components[i].desc == desc) return e->components[i].data;
    }
    e->components = (ComponentData*)realloc(e->components, sizeof(ComponentData) * (e->component_count + 1));
    ComponentData& c = e->components[e->component_count++];
    c.desc = desc;
    c.data = malloc(desc->size);
    memcpy(c.data, desc->defaults, desc->size);
    c.line = 0;
    c.field_lines = nullptr;
    return c.data;
}

void destroy_entity(Scene* s, Entity* e) {
    if (e->destroyed) return;
    e->destroyed = true;
    s->generation++;
}

uint32_t flush_entities(Scene* s) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        Entity& e = s->entities[i];
        if (e.destroyed) {
            for (uint32_t j = 0; j < e.component_count; ++j) {
                free(e.components[j].data);
                free(e.components[j].field_lines);
            }
            free(e.components);
            continue;
        }
        if (kept != i) s->entities[kept] = e;
        ++kept;
    }
    const uint32_t removed = s->entity_count - kept;
    s->entity_count = kept;
    if (removed) s->generation++;
    return removed;
}

namespace {

uint64_t hash_field(const FieldDesc& f, const uint8_t* p, uint64_t h) {
    switch (f.type) {
        case FieldType::kBool: {
            const uint8_t b = *(const bool*)p ? 1 : 0;
            return fnv1a(&b, 1, h);
        }
        case FieldType::kI32:
        case FieldType::kU32:
        case FieldType::kF32:
        case FieldType::kColor:
        case FieldType::kEnum: return fnv1a(p, 4, h);
        case FieldType::kU64: return fnv1a(p, 8, h);
        case FieldType::kVec2: return fnv1a(p, 8, h);
        case FieldType::kVec3: return fnv1a(p, 12, h);
        case FieldType::kVec4: return fnv1a(p, 16, h);
        case FieldType::kString: return fnv1a(p, strlen((const char*)p) + 1, h);
    }
    return h;
}

}  // namespace

uint64_t world_hash(const Scene* s) {
    uint64_t h = fnv1a(&s->next_id, sizeof(s->next_id));
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        const Entity& e = s->entities[i];
        if (e.destroyed) continue;
        h = fnv1a(&e.id, sizeof(e.id), h);
        h = fnv1a(e.name, strlen(e.name) + 1, h);
        for (uint32_t c = 0; c < e.component_count; ++c) {
            const ComponentDesc* d = e.components[c].desc;
            if (reflect::is_visual(d)) continue;
            h = fnv1a(d->name, strlen(d->name) + 1, h);
            const uint8_t* data = (const uint8_t*)e.components[c].data;
            for (uint32_t f = 0; f < d->field_count; ++f) h = hash_field(d->fields[f], data + d->fields[f].offset, h);
        }
    }
    return h;
}

}  // namespace zaro::scene
