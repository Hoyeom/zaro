// Local saves (D-033): the declaration with diagnostics by id and line, reading saves of every
// version through the [[migrate]] steps, damaged, foreign and newer saves, writing and reading
// back, the file store's atomic write with its backup, and save.* in scripts. Runs from the
// repository root (tests/data/save); argv[1] is a scratch directory for the file store.
#include "app/save_layer.h"
#include "core/diag.h"
#include "platform/platform.h"
#include "scene/save_spec.h"
#include "scene/scene.h"
#include "scene/world.h"
#include "script/script.h"
#include "sim/crowd_components.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace zaro;

namespace {

int g_failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

bool has_diag(const DiagList* d, ErrorId id, int line, const char* text = nullptr) {
    for (uint32_t i = 0; i < d->count; ++i) {
        if (d->items[i].id == id && d->items[i].line == line && (!text || strstr(d->items[i].message, text))) return true;
    }
    return false;
}

const scene::SaveValue& value(const scene::SaveData* data, const char* field) {
    return data->values[scene::find_save_field(data->spec, field)];
}

scene::SaveRead read_file(const scene::SaveSpec* spec, const char* path, scene::SaveData* data, DiagList* d) {
    uint64_t size = 0;
    char* text = platform::read_file(path, &size);
    uint32_t version = 0;
    const scene::SaveRead r = text ? scene::read_save_text(spec, text, (size_t)size, path, data, &version, d)
                                   : scene::SaveRead::kDamaged;
    free(text);
    return r;
}

void test_spec() {
    DiagList* d = new DiagList();
    scene::SaveSpec* s = scene::load_save_spec("tests/data/save/save.toml", d);
    expect(s && d->count == 0, "save.toml loads");
    if (s) {
        expect(strcmp(s->game, "savetest") == 0 && s->version == 3 && s->field_count == 4, "game, version, fields");
        const scene::SaveField& best = s->fields[0];
        expect(best.type == scene::kSaveInt && best.has_min && best.min == 0.0 && best.merge == scene::kMergeMax,
               "int field with a range and merge");
        const scene::SaveField& volume = s->fields[1];
        expect(volume.type == scene::kSaveNumber && volume.def.n == 0.8 && volume.local, "local number with a default");
        expect(s->fields[2].type == scene::kSaveString && strcmp(s->fields[2].def.s, "nova") == 0, "string default");
        expect(s->migration_count == 2 && s->migrations[0].from == 1 && s->migrations[0].op_count == 2 &&
                   s->migrations[1].op_count == 1,
               "two migration steps in order");
        const char* hint = scene::suggest_save_field(s, "best_scor");
        expect(hint && strcmp(hint, "best_score") == 0, "did you mean best_score");
    }
    scene::free_save_spec(s);

    d->count = 0;
    s = scene::load_save_spec("tests/data/save/bad_spec.toml", d);
    expect(s == nullptr, "bad_spec.toml fails");
    expect(has_diag(d, kErrSaveSpec, 3, "game must be an id"), "game id, line 3");
    expect(has_diag(d, kErrSaveSpec, 8, "needs type"), "unknown type, line 8");
    expect(has_diag(d, kErrSaveSpec, 13, "above its maximum 1"), "default out of range, line 13");
    expect(has_diag(d, kErrSaveSpec, 19, "merge must be"), "merge on a bool, line 19");
    expect(has_diag(d, kErrSaveSpec, 22, "needs from"), "migration from the current version, line 22");
    if (g_failures) diag_print_text(d, stderr);
    delete d;
}

void test_read(const scene::SaveSpec* spec) {
    DiagList* d = new DiagList();
    scene::SaveData* data = new scene::SaveData();

    // Version 1: best renamed, old_flag removed, coins set by the step from 2, pilot from its default.
    expect(read_file(spec, "tests/data/save/v1.toml", data, d) == scene::SaveRead::kMigrated && d->count == 0,
           "v1 migrates without problems");
    expect(value(data, "best_score").i == 42 && value(data, "coins").i == 100 && value(data, "music_volume").n == 0.5 &&
               strcmp(value(data, "pilot").s, "nova") == 0,
           "v1 values after migration");

    // Version 2: only the step from 2 runs.
    d->count = 0;
    expect(read_file(spec, "tests/data/save/v2.toml", data, d) == scene::SaveRead::kMigrated && d->count == 0, "v2 migrates");
    expect(value(data, "best_score").i == 7 && value(data, "coins").i == 100 && strcmp(value(data, "pilot").s, "kite") == 0,
           "v2 values after migration");

    // Newer, damaged and foreign saves leave the defaults.
    d->count = 0;
    expect(read_file(spec, "tests/data/save/newer.toml", data, d) == scene::SaveRead::kNewer &&
               has_diag(d, kErrSaveNewer, 3, "newer than this one") && value(data, "best_score").i == 0,
           "a newer save is not loaded");
    d->count = 0;
    expect(read_file(spec, "tests/data/save/damaged.toml", data, d) == scene::SaveRead::kDamaged && d->count == 1,
           "a damaged save");
    d->count = 0;
    expect(read_file(spec, "tests/data/save/other_game.toml", data, d) == scene::SaveRead::kDamaged &&
               has_diag(d, kErrSaveFile, 2, "belongs to game 'neon'"),
           "another game's save");

    // Values that do not fit take the default; the others still load.
    d->count = 0;
    expect(read_file(spec, "tests/data/save/bad_values.toml", data, d) == scene::SaveRead::kLoaded, "bad values load");
    expect(has_diag(d, kErrSaveFile, 6, "below its minimum 0") && has_diag(d, kErrSaveFile, 7, "must be a string") &&
               has_diag(d, kErrSaveFile, 8, "must be an int") && has_diag(d, kErrSaveFile, 9, "did you mean 'best_score'"),
           "each bad value is reported at its line");
    expect(value(data, "best_score").i == 0 && value(data, "coins").i == 0 && value(data, "music_volume").n == 0.25,
           "defaults for the bad values, the good one kept");

    // Writing and reading back gives the same values, strings escaped.
    scene::reset_save_data(data, spec);
    char err[300];
    scene::SaveValue v = {};
    v.type = scene::kSaveString;
    snprintf(v.s, sizeof(v.s), "quote \" back\\slash\nnew line 한글");
    expect(scene::set_save_value(data, 2, v, err, sizeof(err)), "set a string");
    v = {};
    v.type = scene::kSaveNumber;
    v.n = 0.1;
    expect(scene::set_save_value(data, 1, v, err, sizeof(err)), "set a number");
    v.n = 12.0;
    expect(scene::set_save_value(data, 0, v, err, sizeof(err)), "a whole number fits an int");
    v.n = 1.5;
    expect(!scene::set_save_value(data, 0, v, err, sizeof(err)) && strstr(err, "must be an int"), "1.5 is not an int");
    v.n = 2.0;
    expect(!scene::set_save_value(data, 1, v, err, sizeof(err)) && strstr(err, "above its maximum 1"), "out of range");
    size_t size = 0;
    char* text = scene::write_save_text(data, &size);
    scene::SaveData* back = new scene::SaveData();
    uint32_t version = 0;
    d->count = 0;
    expect(scene::read_save_text(spec, text, size, "written", back, &version, d) == scene::SaveRead::kLoaded &&
               d->count == 0 && version == 3,
           "the written save reads back");
    expect(value(back, "best_score").i == 12 && value(back, "music_volume").n == 0.1 &&
               strcmp(value(back, "pilot").s, value(data, "pilot").s) == 0,
           "same values after a round trip");
    if (d->count) {
        fputs(text, stderr);
        diag_print_text(d, stderr);
    }
    free(text);
    delete back;
    delete data;
    delete d;
}

void test_file_store(const scene::SaveSpec* spec, const char* scratch) {
    char dir[600], path[700], bak[720];
    snprintf(dir, sizeof(dir), "%s/save_store/", scratch);
    snprintf(path, sizeof(path), "%ssave.toml", dir);
    snprintf(bak, sizeof(bak), "%ssave.toml.bak", dir);
    platform::create_directories(dir);
    remove(path);
    remove(bak);
    DiagList* d = new DiagList();
    scene::SaveData* data = new scene::SaveData();
    scene::reset_save_data(data, spec);

    // A new game loads the defaults; two commits leave the newest in save.toml and the first in .bak.
    app::SaveStore store = app::file_save_store(dir);
    expect(store.load(store.user, spec, data, d) == scene::SaveRead::kLoaded && d->count == 0, "no save yet");
    data->values[0].i = 5;
    expect(store.commit(store.user, data, d), "first commit");
    data->values[0].i = 6;
    expect(store.commit(store.user, data, d), "second commit");
    scene::SaveData* got = new scene::SaveData();
    expect(read_file(spec, path, got, d) == scene::SaveRead::kLoaded && got->values[0].i == 6, "save.toml has the newest");
    expect(read_file(spec, bak, got, d) == scene::SaveRead::kLoaded && got->values[0].i == 5, "the backup has the one before");
    store.destroy(store.user);

    // A damaged save.toml: the backup loads, with a diagnostic.
    FILE* f = fopen(path, "wb");
    if (f) {
        fputs("[save\n", f);
        fclose(f);
    }
    store = app::file_save_store(dir);
    d->count = 0;
    expect(store.load(store.user, spec, got, d) == scene::SaveRead::kLoaded && got->values[0].i == 5 &&
               d->count == 1 && strstr(d->items[0].message, "loaded the backup"),
           "a damaged save falls back to the backup");
    store.destroy(store.user);

    // A newer game's save is neither loaded nor overwritten.
    f = fopen(path, "wb");
    const char* newer = "[save]\ngame = \"savetest\"\nversion = 9\n[data]\nbest_score = 1000\n";
    if (f) {
        fputs(newer, f);
        fclose(f);
    }
    store = app::file_save_store(dir);
    d->count = 0;
    expect(store.load(store.user, spec, got, d) == scene::SaveRead::kNewer && got->values[0].i == 0, "newer: defaults");
    expect(!store.commit(store.user, data, d) && has_diag(d, kErrSaveNewer, 0, "not saving"), "newer: no commit");
    uint64_t size = 0;
    char* text = platform::read_file(path, &size);
    expect(text && strcmp(text, newer) == 0, "the newer save is untouched");
    free(text);
    store.destroy(store.user);

    remove(path);
    remove(bak);
    delete got;
    delete data;
    delete d;
}

void test_script(const scene::SaveSpec* spec) {
    DiagList* d = new DiagList();
    scene::Scene* sc = scene::load_scene("tests/data/save/scene.toml", d);
    expect(sc && strstr(sc->save, "tests/data/save/save.toml"), "[scene] save");
    scene::SaveData* data = new scene::SaveData();
    scene::reset_save_data(data, spec);
    script::ScriptDesc desc;
    desc.scene = sc;
    desc.save = data;
    const char* path = "tests/data/save/scratch.luau";
    struct Case {
        const char* code;
        int line;
        const char* message;
    };
    const Case cases[] = {
        {"function on_start()\n    save.set(\"pilot\", \"ace\")\n    save.commit()\nend\n", 0, nullptr},
        {"function on_start()\n    save.get(\"best_scor\")\nend\n", 2, "declares no field 'best_scor' (did you mean 'best_score'?)"},
        {"function on_start()\n    save.set(\"coins\", 1.5)\nend\n", 2, "coins must be an int"},
        {"function on_start()\n    save.set(\"music_volume\", 3)\nend\n", 2, "above its maximum 1"},
        {"function on_start()\n    save.set(\"pilot\", {})\nend\n", 2, "expects a number, boolean or string"},
    };
    for (const Case& c : cases) {
        FILE* f = fopen(path, "wb");
        if (f) {
            fputs(c.code, f);
            fclose(f);
        }
        desc.path = path;
        d->count = 0;
        script::Script* s = sc ? script::load_script(desc, d) : nullptr;
        const bool ok = s && script::call_start(s, d);
        if (!c.message) {
            expect(ok && strcmp(value(data, "pilot").s, "ace") == 0 && data->commit_requested, "save.set and save.commit");
        } else {
            expect(!ok && has_diag(d, kErrScriptRuntime, c.line, c.message), c.message);
        }
        script::destroy_script(s);
    }
    remove(path);
    delete data;
    scene::free_scene(sc);
    delete d;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scratch = argc > 1 ? argv[1] : ".";
    sim::register_crowd_components();
    scene::register_scene_components();
    test_spec();
    DiagList* d = new DiagList();
    scene::SaveSpec* spec = scene::load_save_spec("tests/data/save/save.toml", d);
    if (spec) {
        test_read(spec);
        test_file_store(spec, scratch);
        test_script(spec);
    }
    scene::free_save_spec(spec);
    delete d;
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("save: all tests passed\n");
    return 0;
}
