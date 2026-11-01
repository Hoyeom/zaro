// Luau scripts: hooks and the zaro API, error ids and lines, sandbox, time budget, seeded random,
// hot reload. argv[1] is a writable directory for the reload test; run from the repository root.
#include "core/detmath.h"
#include "core/diag.h"
#include "scene/scene.h"
#include "scene/tween.h"
#include "scene/world.h"
#include "script/script.h"
#include "sim/crowd_components.h"
#include "sim/sprite_components.h"

#include <math.h>
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

zaro::scene::Scene* load_test_scene(const char* path = "tests/data/scripts/scene.toml") {
    zaro::DiagList* diags = new zaro::DiagList();
    zaro::scene::Scene* s = zaro::scene::load_scene(path, diags);
    if (!s) zaro::diag_print_text(diags, stderr);
    delete diags;
    return s;
}

const zaro::sim::CrowdSpawner* horde(const zaro::scene::Scene* s) {
    return (const zaro::sim::CrowdSpawner*)zaro::scene::get_component(zaro::scene::find_entity(s, "horde"),
                                                                       &zaro::sim::kCrowdSpawnerDesc);
}

struct Run {
    zaro::scene::Scene* scene = nullptr;
    zaro::script::Script* script = nullptr;
    zaro::DiagList* diags = new zaro::DiagList();

    explicit Run(const char* path, uint64_t seed = 1, double budget = 1.0,
                 const char* scene_path = "tests/data/scripts/scene.toml") {
        scene = load_test_scene(scene_path);
        zaro::script::ScriptDesc d;
        d.path = path;
        d.scene = scene;
        d.seed = seed;
        d.max_call_seconds = budget;
        if (scene) script = zaro::script::load_script(d, diags);
    }
    ~Run() {
        zaro::script::destroy_script(script);
        zaro::scene::free_scene(scene);
        delete diags;
    }
    bool has(zaro::ErrorId id, int line, const char* needle = nullptr) const {
        for (uint32_t i = 0; i < diags->count; ++i) {
            const zaro::Diag& d = diags->items[i];
            if (d.id == id && d.line == line && (!needle || strstr(d.message, needle))) return true;
        }
        zaro::diag_print_text(diags, stderr);
        return false;
    }
};

void test_hooks() {
    Run r("tests/data/scripts/ok.luau");
    expect(r.script != nullptr, "ok.luau loads");
    if (!r.script) return;
    expect(zaro::script::call_start(r.script, r.diags), "on_start runs");
    expect(horde(r.scene)->speed == 8.0f, "on_start doubled the speed through zaro.get/zaro.set");
    for (uint64_t t = 1; t <= 3; ++t) expect(zaro::script::call_tick(r.script, t, r.diags), "on_tick runs");
    expect(horde(r.scene)->count == 103, "on_tick set count = 100 + tick");
}

void test_errors() {
    {
        Run r("tests/data/scripts/syntax.luau");
        expect(r.script == nullptr, "syntax error rejects the script");
        expect(r.has(zaro::kErrScriptCompile, 3), "syntax error is ZE0500 on line 3");
    }
    {
        Run r("tests/data/scripts/runtime.luau");
        expect(r.script && !zaro::script::call_tick(r.script, 1, r.diags), "misspelt field fails the tick");
        expect(r.has(zaro::kErrScriptRuntime, 2, "did you mean 'speed'"), "runtime error is ZE0501 on line 2 with a hint");
    }
    {
        Run r("tests/data/scripts/loop.luau", 1, 0.2);
        expect(r.script && !zaro::script::call_tick(r.script, 1, r.diags), "infinite loop is aborted");
        bool timeout = false;
        for (uint32_t i = 0; i < r.diags->count; ++i) timeout |= r.diags->items[i].id == zaro::kErrScriptTimeout;
        expect(timeout, "infinite loop reports ZE0502");
    }
}

void test_sandbox() {
    Run r("tests/data/scripts/sandbox.luau");
    expect(r.script && zaro::script::call_start(r.script, r.diags), "sandbox asserts hold");
    if (r.diags->count) zaro::diag_print_text(r.diags, stderr);
}

uint32_t random_count(uint64_t seed) {
    Run r("tests/data/scripts/random.luau", seed);
    if (!r.script || !zaro::script::call_start(r.script, r.diags)) return 0;
    return horde(r.scene)->count;
}

void test_random() {
    const uint32_t a = random_count(7), b = random_count(7), c = random_count(8);
    expect(a != 0 && a == b, "same seed gives the same math.random sequence");
    expect(a != c, "a different seed gives a different sequence");
}

void write_file(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

void test_reload(const char* dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/reload_test.luau", dir);
    write_file(path, "function on_tick(t) zaro.set('horde', 'CrowdSpawner', 'count', 1) end\n");
    Run r(path);
    expect(r.script && zaro::script::call_tick(r.script, 1, r.diags) && horde(r.scene)->count == 1, "version 1 runs");
    write_file(path, "function on_tick(t) zaro.set('horde', 'CrowdSpawner', 'count', 2) end\n");
    expect(r.script && zaro::script::reload(r.script, r.diags), "reload succeeds");
    expect(r.script && zaro::script::call_tick(r.script, 2, r.diags) && horde(r.scene)->count == 2,
           "reloaded hook replaces the old one");
    write_file(path, "function on_tick(t) = end\n");
    expect(r.script && !zaro::script::reload(r.script, r.diags), "broken reload is rejected");
    expect(r.script && zaro::script::call_tick(r.script, 3, r.diags) && horde(r.scene)->count == 2,
           "old code keeps running after a broken reload");
    remove(path);
}

// Spawning, queries and destroying (D-031): world.luau asserts what scripts see, this checks the scene.
uint64_t run_world(uint32_t* entities_after) {
    Run r("tests/data/scripts/world.luau", 1, 1.0, "tests/data/scripts/world_scene.toml");
    expect(r.script && zaro::script::call_start(r.script, r.diags), "world.luau on_start");
    if (!r.script) return 0;
    expect(r.scene->entity_count == 6, "two file entities and four spawned");
    const uint64_t g0 = r.scene->generation;
    expect(zaro::script::call_tick(r.script, 1, r.diags), "world.luau tick 1 destroys a grunt");
    expect(r.scene->entity_count == 6 && r.scene->generation != g0, "destroy marks the entity until the flush");
    expect(zaro::scene::flush_entities(r.scene) == 1 && r.scene->entity_count == 5, "the flush removes it");
    expect(zaro::script::call_tick(r.script, 2, r.diags), "world.luau tick 2");
    const zaro::scene::Entity* g = zaro::scene::entity_by_id(r.scene, 3);
    const auto* sp = g ? (const zaro::sim::CrowdSpawner*)zaro::scene::get_component(g, &zaro::sim::kCrowdSpawnerDesc) : nullptr;
    expect(sp && sp->speed == 9.0f, "zaro.set by id reached grunt#3");
    expect(!zaro::scene::entity_by_id(r.scene, 4) && zaro::scene::entity_by_id(r.scene, 5), "ids stay after removal");
    if (r.diags->count) zaro::diag_print_text(r.diags, stderr);
    *entities_after = r.scene->entity_count;
    return zaro::scene::world_hash(r.scene);
}

void test_world() {
    uint32_t n1 = 0, n2 = 0;
    const uint64_t h1 = run_world(&n1);
    const uint64_t h2 = run_world(&n2);
    expect(h1 && h1 == h2 && n1 == n2, "the same script gives the same world hash");

    Run r("tests/data/scripts/world_errors.luau", 1, 1.0, "tests/data/scripts/world_scene.toml");
    expect(r.script != nullptr, "world_errors.luau loads");
    if (!r.script) return;
    expect(!zaro::script::call_tick(r.script, 1, r.diags) && r.has(zaro::kErrScriptRuntime, 4, "did you mean 'grunt'"),
           "unknown prefab: ZE0501 with a hint");
    expect(!zaro::script::call_tick(r.script, 2, r.diags) && r.has(zaro::kErrScriptRuntime, 6, "did you mean 'player'"),
           "unknown entity name: ZE0501 with a hint");
    expect(!zaro::script::call_tick(r.script, 3, r.diags) && r.has(zaro::kErrScriptRuntime, 8, "no entity with id 99"),
           "unknown id");
    expect(!zaro::script::call_tick(r.script, 4, r.diags) && r.has(zaro::kErrScriptRuntime, 10, "did you mean 'CrowdSpawner'"),
           "unknown component in spawn options");
    expect(!zaro::script::call_tick(r.script, 5, r.diags) && r.has(zaro::kErrScriptRuntime, 12, "already exists"),
           "spawn with a taken name");
}

// Timers, events and engine events (D-031): events.luau asserts the order on tick 91.
void test_events() {
    {
        Run r("tests/data/scripts/events.luau");
        expect(r.script && zaro::script::call_start(r.script, r.diags), "events.luau on_start");
        if (!r.script) return;
        bool ok = true;
        for (uint64_t t = 1; t <= 91 && ok; ++t) {
            if (t == 10) {
                const char* keys[] = {"speed"};
                const double values[] = {6.5};
                expect(zaro::script::emit_event(r.script, "engine", keys, values, 1), "emit_event queues");
            }
            ok = zaro::script::call_tick(r.script, t, r.diags);
        }
        expect(ok, "timers and events fire in the documented order");
        expect(horde(r.scene)->speed == 6.5f, "an engine event reached its handler");
        if (!ok) zaro::diag_print_text(r.diags, stderr);
    }
    {
        Run r("tests/data/scripts/storm.luau");
        expect(r.script && zaro::script::call_start(r.script, r.diags), "storm.luau on_start");
        if (!r.script) return;
        expect(!zaro::script::call_tick(r.script, 1, r.diags) && r.has(zaro::kErrEventLimit, 0, "'ping'"),
               "an event loop stops with ZE1201");
        r.diags->count = 0;
        expect(zaro::script::call_tick(r.script, 2, r.diags), "the next tick starts with an empty queue");
    }
}

// Parents and world transforms (D-031): hierarchy.luau asserts positions; this checks the cascade.
void test_hierarchy() {
    Run r("tests/data/scripts/hierarchy.luau", 1, 1.0, "tests/data/scripts/hierarchy_scene.toml");
    expect(r.script && zaro::script::call_start(r.script, r.diags), "hierarchy.luau on_start");
    if (!r.script) return;
    expect(zaro::script::call_tick(r.script, 1, r.diags), "a turned parent carries its children");
    expect(zaro::script::call_tick(r.script, 2, r.diags), "destroying a parent destroys its children");
    expect(zaro::scene::flush_entities(r.scene) == 3 && r.scene->entity_count == 1, "only the comet is left");
    if (r.diags->count) zaro::diag_print_text(r.diags, stderr);
}

// core/detmath.h agrees with the C library to well below float precision.
void test_detmath() {
    double worst = 0.0;
    for (int i = -7200; i <= 7200; ++i) {
        const double deg = i * 0.37;
        double s = 0.0, c = 0.0;
        zaro::det_sincos_deg(deg, &s, &c);
        const double r = deg * (3.14159265358979323846 / 180.0);
        worst = fmax(worst, fmax(fabs(s - sin(r)), fabs(c - cos(r))));
    }
    expect(worst < 1e-12, "det_sincos_deg matches sin/cos");
    double worst_exp = 0.0;
    for (int i = -400; i <= 400; ++i) {
        const double x = i * 0.073;
        worst_exp = fmax(worst_exp, fabs(zaro::det_exp2(x) / exp2(x) - 1.0));
    }
    expect(worst_exp < 1e-13, "det_exp2 matches exp2");
}

// Tweens (D-031): tween.luau asserts values; this steps the tweens after each tick like the app.
void test_tweens() {
    Run r("tests/data/scripts/tween.luau", 1, 1.0, "tests/data/scripts/tween_scene.toml");
    expect(r.script && zaro::script::call_start(r.script, r.diags), "tween.luau on_start");
    if (!r.script) return;
    bool ok = true;
    for (uint64_t t = 1; t <= 70 && ok; ++t) {
        r.scene->tick = t;
        ok = zaro::script::call_tick(r.script, t, r.diags);
        uint32_t done[16];
        const uint32_t n = zaro::scene::step_tweens(r.scene, t, nullptr, nullptr, done, 16);
        ok = ok && zaro::script::finish_tweens(r.script, done, n, r.diags);
    }
    expect(ok, "tweens from scripts, prefabs and the scene file follow their curves");
    if (!ok) zaro::diag_print_text(r.diags, stderr);
    expect(zaro::scene::ease(zaro::scene::kEaseOutBounce, 1.0) == 1.0 && zaro::scene::ease(zaro::scene::kEaseInExpo, 0.0) == 0.0,
           "eases start at 0 and end at 1");
}

}  // namespace

int main(int argc, char** argv) {
    zaro::sim::register_crowd_components();
    zaro::scene::register_scene_components();
    zaro::sim::register_sprite_components();
    test_hooks();
    test_errors();
    test_sandbox();
    test_random();
    test_reload(argc > 1 ? argv[1] : ".");
    test_world();
    test_events();
    test_hierarchy();
    test_detmath();
    test_tweens();
    if (g_failures) {
        fprintf(stderr, "test_script: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_script: ok\n");
    return 0;
}
