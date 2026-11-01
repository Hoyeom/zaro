#include "scene/world.h"

#include "core/detmath.h"
#include "core/hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

using reflect::ComponentDesc;
using reflect::FieldDesc;
using reflect::FieldType;

namespace {

const reflect::FieldDesc kTransformFields[] = {
    ZARO_FIELD(Transform, position, kVec3, "Position relative to the parent (or the world), in metres"),
    ZARO_FIELD(Transform, rotation, kF32, "Turn about +y in degrees, relative to the parent; +x turns toward +z"),
    ZARO_FIELD_RANGE(Transform, scale, kF32, "Size relative to the parent; children are scaled with it", 0.0, 1000.0),
};

}  // namespace

ZARO_COMPONENT(Transform, "Where an entity is relative to its parent; a parent moves, turns and scales its children",
               kTransformFields);

void register_scene_components() { reflect::register_component(&kTransformDesc); }

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

namespace {

// The index of an entity by id, destroyed or not, or -1. Ids ascend with the index.
int index_of(const Scene* s, uint32_t id) {
    uint32_t lo = 0, hi = s->entity_count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (s->entities[mid].id < id) lo = mid + 1;
        else hi = mid;
    }
    return lo < s->entity_count && s->entities[lo].id == id ? (int)lo : -1;
}

}  // namespace

Entity* entity_by_id(Scene* s, uint32_t id) {
    const int i = index_of(s, id);
    return i >= 0 && !s->entities[i].destroyed ? &s->entities[i] : nullptr;
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
    char given[kNameCapacity + 16];  // room for "<prefab>#<id>"; cut to kNameCapacity below
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
    snprintf(e->name, sizeof(e->name), "%.*s", (int)(kNameCapacity - 1), given);
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
    // Children go with their parent, and theirs with them.
    for (bool more = true; more;) {
        more = false;
        for (uint32_t i = 0; i < s->entity_count; ++i) {
            Entity& c = s->entities[i];
            if (c.destroyed || !c.parent_id) continue;
            const int p = index_of(s, c.parent_id);
            if (p >= 0 && s->entities[p].destroyed) more = c.destroyed = true;
        }
    }
    s->generation++;
}

Entity* parent_of(Scene* s, const Entity* e) { return e->parent_id ? entity_by_id(s, e->parent_id) : nullptr; }

bool set_parent(Scene* s, Entity* child, Entity* parent, char* err, uint32_t err_size) {
    for (Entity* p = parent; p; p = parent_of(s, p)) {
        if (p == child) {
            snprintf(err, err_size, "'%s' cannot be a child of '%s', which is already below it", child->name,
                     parent->name);
            return false;
        }
    }
    child->parent_id = parent ? parent->id : 0;
    return true;
}

namespace {

const Transform* transform_of(const Entity* e) { return (const Transform*)get_component(e, &kTransformDesc); }

// parent * local: the child's transform in the parent's space, placed in the world.
WorldTransform combine(const WorldTransform& p, const Transform* local) {
    if (!local) return p;
    WorldTransform w;
    transform_point(p, local->position, w.position);
    w.rotation = p.rotation + local->rotation;
    w.scale = p.scale * local->scale;
    return w;
}

}  // namespace

void transform_point(const WorldTransform& w, const float local[3], float out[3]) {
    double sn = 0.0, cs = 1.0;
    if (w.rotation != 0.0f) det_sincos_deg(w.rotation, &sn, &cs);
    const double x = (double)local[0] * w.scale, y = (double)local[1] * w.scale, z = (double)local[2] * w.scale;
    out[0] = (float)(w.position[0] + (x * cs - z * sn));
    out[1] = (float)(w.position[1] + y);
    out[2] = (float)(w.position[2] + (x * sn + z * cs));
}

WorldTransform world_transform(Scene* s, const Entity* e) {
    // The chain from the entity up to the top; set_parent keeps it free of loops.
    constexpr int kMaxDepth = 64;
    const Entity* chain[kMaxDepth];
    int n = 0;
    for (const Entity* p = e; p && n < kMaxDepth; p = parent_of(s, p)) chain[n++] = p;
    WorldTransform w{{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f};
    for (int i = n - 1; i >= 0; --i) w = combine(w, transform_of(chain[i]));
    return w;
}

void update_transforms(Scene* s) {
    for (uint32_t i = 0; i < s->entity_count; ++i) {
        if (!s->entities[i].destroyed) s->entities[i].world = world_transform(s, &s->entities[i]);
    }
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
        if (e.parent_id) h = fnv1a(&e.parent_id, sizeof(e.parent_id), h);
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
