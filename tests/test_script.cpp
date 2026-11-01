// Luau scripts: hooks and the zaro API, error ids and lines, sandbox, time budget, seeded random,
// hot reload. argv[1] is a writable directory for the reload test; run from the repository root.
#include "core/diag.h"
#include "scene/scene.h"
#include "script/script.h"
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

zaro::scene::Scene* load_test_scene() {
    zaro::DiagList* diags = new zaro::DiagList();
    zaro::scene::Scene* s = zaro::scene::load_scene("tests/data/scripts/scene.toml", diags);
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

    explicit Run(const char* path, uint64_t seed = 1, double budget = 1.0) {
        scene = load_test_scene();
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

}  // namespace

int main(int argc, char** argv) {
    zaro::sim::register_crowd_components();
    test_hooks();
    test_errors();
    test_sandbox();
    test_random();
    test_reload(argc > 1 ? argv[1] : ".");
    if (g_failures) {
        fprintf(stderr, "test_script: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_script: ok\n");
    return 0;
}
