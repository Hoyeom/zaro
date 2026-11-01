// Scene loading: prefab overrides, defaults, and one diagnostic per problem with the right id
// and line. Run from the repository root (ctest sets WORKING_DIRECTORY).
#include "core/diag.h"
#include "core/reflect.h"
#include "scene/scene.h"
#include "sim/crowd_components.h"

#include <stdio.h>
#include <string.h>

namespace {

int g_failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void test_override() {
    zaro::DiagList* diags = new zaro::DiagList();
    zaro::scene::Scene* s = zaro::scene::load_scene("tests/data/scenes/override.toml", diags);
    expect(s != nullptr, "override.toml loads");
    if (s) {
        const zaro::scene::Entity* e = zaro::scene::find_entity(s, "horde");
        expect(e != nullptr, "entity 'horde' exists");
        const auto* spawner = e ? (const zaro::sim::CrowdSpawner*)zaro::scene::get_component(
                                      e, &zaro::sim::kCrowdSpawnerDesc)
                                : nullptr;
        expect(spawner != nullptr, "horde has CrowdSpawner");
        if (spawner) {
            expect(spawner->count == 123, "entity value overrides the prefab (count = 123)");
            expect(spawner->speed == 6.5f, "prefab value is kept (speed = 6.5)");
            expect(spawner->radius == zaro::sim::CrowdSpawner{}.radius, "default is kept (radius)");
        }
    } else {
        zaro::diag_print_text(diags, stderr);
    }
    zaro::scene::free_scene(s);
    delete diags;
}

void test_errors() {
    struct Expected {
        zaro::ErrorId id;
        int line;
    };
    // One entry per problem in tests/data/scenes/errors.toml, in file order of discovery.
    const Expected expected[] = {
        {zaro::kErrSceneStructure, 4},         // unknown key in [scene]
        {zaro::kErrSceneUnknownComponent, 8},  // CrowdSpwner
        {zaro::kErrSceneUnknownField, 14},     // cuont
        {zaro::kErrSceneType, 15},             // speed = "fast"
        {zaro::kErrSceneRange, 16},            // radius = 99
        {zaro::kErrScenePrefab, 20},           // missing.toml
        {zaro::kErrSceneDuplicate, 18},        // second 'a'
        {zaro::kErrSceneStructure, 22},        // entity without name
    };
    const uint32_t n = sizeof(expected) / sizeof(expected[0]);
    zaro::DiagList* diags = new zaro::DiagList();
    zaro::scene::Scene* s = zaro::scene::load_scene("tests/data/scenes/errors.toml", diags);
    expect(s == nullptr, "errors.toml is rejected");
    expect(diags->count == n, "errors.toml reports exactly one diagnostic per problem");
    for (uint32_t i = 0; i < n && i < diags->count; ++i) {
        char what[96];
        snprintf(what, sizeof(what), "diagnostic %u is ZE%04d on line %d", i, (int)expected[i].id, expected[i].line);
        expect(diags->items[i].id == expected[i].id && diags->items[i].line == expected[i].line, what);
    }
    expect(diags->count > 1 && strstr(diags->items[1].message, "did you mean 'CrowdSpawner'") != nullptr,
           "unknown component suggests the closest name");
    if (g_failures) zaro::diag_print_text(diags, stderr);
    zaro::scene::free_scene(s);
    delete diags;
}

}  // namespace

int main() {
    zaro::sim::register_crowd_components();
    test_override();
    test_errors();
    if (g_failures) {
        fprintf(stderr, "test_scene: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_scene: ok\n");
    return 0;
}
