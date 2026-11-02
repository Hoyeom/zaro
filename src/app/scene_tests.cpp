// `zaro test`: runs *.test.toml files (format in src/scene/test_spec.h) without a GPU.
#include "app/audio_layer.h"
#include "app/crowd_app.h"
#include "app/save_layer.h"
#include "app/tools.h"
#include "app/ui_layer.h"
#include "core/diag.h"
#include "platform/platform.h"
#include "scene/scene.h"
#include "scene/test_spec.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

namespace {

struct FileList {
    static constexpr uint32_t kMax = 512;
    char* paths[kMax];
    uint32_t count = 0;
};

void collect(const char* path, void* user) {
    FileList* list = (FileList*)user;
    if (list->count < FileList::kMax) {
        const size_t n = strlen(path) + 1;
        list->paths[list->count] = (char*)malloc(n);
        memcpy(list->paths[list->count], path, n);
        list->count++;
    }
}

int compare_paths(const void* a, const void* b) { return strcmp(*(char* const*)a, *(char* const*)b); }

// The scene, or for a screen expectation the shown screen's file, that the expectation names.
const scene::Scene* expect_document(const scene::Scene* s, UiLayer* ui, const scene::Expect& e) {
    if (!e.screen[0]) return s;
    return ui ? find_document(ui, e.screen) : nullptr;
}

// Reads a field after the run as a number (booleans as 0/1) or a string.
bool read_field(const scene::Scene* s, const scene::Expect& e, double* number, const char** string,
                char (&text)[reflect::kStringCapacity]) {
    if (!s) return false;
    const scene::Entity* ent = scene::find_entity(s, e.entity);
    if (!ent) return false;
    const unsigned char* data = (const unsigned char*)scene::get_component(ent, e.component);
    if (!data) return false;
    const unsigned char* p = data + e.field->offset;
    switch (e.field->type) {
        case reflect::FieldType::kBool: *number = *(const bool*)p ? 1.0 : 0.0; return true;
        case reflect::FieldType::kI32: *number = *(const int32_t*)p; return true;
        case reflect::FieldType::kU32: *number = *(const uint32_t*)p; return true;
        case reflect::FieldType::kU64: *number = (double)*(const uint64_t*)p; return true;
        case reflect::FieldType::kF32: *number = *(const float*)p; return true;
        case reflect::FieldType::kString: *string = (const char*)p; return true;
        case reflect::FieldType::kColor:
        case reflect::FieldType::kEnum:
            reflect::format_text_value(*e.field, p, text, sizeof(text));
            *string = text;
            return true;
        default: return false;
    }
}

bool nearly_equal(double a, double b) { return fabs(a - b) <= 1e-6 * (fabs(a) > 1.0 ? fabs(a) : 1.0); }

// A sound's play count or a group's volume against equals, min and max.
void check_audio_expect(const scene::TestSpec& spec, const scene::Expect& e, const AudioLayer* audio, DiagList* d) {
    const char* what = e.sound[0] ? "sound" : "volume group";
    const char* name = e.sound[0] ? e.sound : e.volume;
    const double x = e.sound[0] ? (double)sound_play_count(audio, name) : sound_group_volume(audio, name);
    if (!audio || x < 0.0) {
        diag_add(d, kErrTestExpect, spec.path, e.line, 1, "the scene has no %s '%s'", what, name);
        return;
    }
    const char* label = e.sound[0] ? "plays" : "volume";
    if (e.has_equals && !nearly_equal(x, e.equals_number)) {
        diag_add(d, kErrTestExpect, spec.path, e.line, 1, "%s '%s': %s %g, expected %g", what, name, label, x,
                 e.equals_number);
    }
    if (e.has_min && x < e.min) {
        diag_add(d, kErrTestExpect, spec.path, e.line, 1, "%s '%s': %s %g, expected at least %g", what, name, label, x,
                 e.min);
    }
    if (e.has_max && x > e.max) {
        diag_add(d, kErrTestExpect, spec.path, e.line, 1, "%s '%s': %s %g, expected at most %g", what, name, label, x,
                 e.max);
    }
}

// A save field as last committed.
void check_save_expect(const scene::TestSpec& spec, const scene::Expect& e, const SaveLayer* save, DiagList* d) {
    double x = 0.0;
    const char* str = nullptr;
    if (!committed_value(save, e.save, &x, &str)) {
        diag_add(d, kErrTestExpect, spec.path, e.line, 1, "the scene's save declares no field '%s'", e.save);
        return;
    }
    if (str || e.equals_string[0]) {
        if (!str || !e.has_equals || strcmp(str, e.equals_string) != 0) {
            diag_add(d, kErrTestExpect, spec.path, e.line, 1, "save '%s' = \"%s\", expected \"%s\"", e.save,
                     str ? str : "(a number)", e.equals_string);
        }
        return;
    }
    if (e.has_equals && !nearly_equal(x, e.equals_number)) {
        diag_add(d, kErrTestExpect, spec.path, e.line, 1, "save '%s' = %g, expected %g", e.save, x, e.equals_number);
    }
    if (e.has_min && x < e.min) {
        diag_add(d, kErrTestExpect, spec.path, e.line, 1, "save '%s' = %g, expected at least %g", e.save, x, e.min);
    }
    if (e.has_max && x > e.max) {
        diag_add(d, kErrTestExpect, spec.path, e.line, 1, "save '%s' = %g, expected at most %g", e.save, x, e.max);
    }
}

void check_expects(const scene::TestSpec& spec, const scene::Scene* s, UiLayer* ui, const AudioLayer* audio,
                   const SaveLayer* save, DiagList* d) {
    for (uint32_t i = 0; i < spec.expect_count; ++i) {
        const scene::Expect& e = spec.expects[i];
        if (e.save[0]) {
            check_save_expect(spec, e, save, d);
            continue;
        }
        if (e.sound[0] || e.volume[0]) {
            check_audio_expect(spec, e, audio, d);
            continue;
        }
        if (e.has_state) {
            const char* got = ui ? screen_state(ui, e.screen) : "closed";
            if (strcmp(got, e.state) != 0) {
                diag_add(d, kErrTestExpect, spec.path, e.line, 1, "screen '%s' is %s, expected %s", e.screen, got,
                         e.state);
            }
            continue;
        }
        if (e.has_text) {
            char shown[1024];
            const char* got = ui ? node_text(ui, e.screen, e.entity, shown, sizeof(shown)) : nullptr;
            if (!got) {
                diag_add(d, kErrTestExpect, spec.path, e.line, 1, "screen '%s' has no node '%s'", e.screen, e.entity);
            } else if (strcmp(got, e.text) != 0) {
                diag_add(d, kErrTestExpect, spec.path, e.line, 1, "%s/%s shows \"%s\", expected \"%s\"", e.screen,
                         e.entity, got, e.text);
            }
            continue;
        }
        if (!e.component || !e.field) continue;
        double x = 0.0;
        const char* str = nullptr;
        char text[reflect::kStringCapacity];
        if (!read_field(expect_document(s, ui, e), e, &x, &str, text)) {
            diag_add(d, kErrTestExpect, spec.path, e.line, 1, "%s%s%s '%s' with a %s component not found after the run",
                     e.screen[0] ? "screen '" : "", e.screen, e.screen[0] ? "' node" : "entity", e.entity,
                     e.component->name);
            continue;
        }
        const char* name = e.field->name;
        if (str) {
            if (e.has_equals && strcmp(str, e.equals_string) != 0) {
                diag_add(d, kErrTestExpect, spec.path, e.line, 1, "%s.%s.%s = \"%s\", expected \"%s\"", e.entity,
                         e.component->name, name, str, e.equals_string);
            }
            continue;
        }
        if (e.has_equals && !nearly_equal(x, e.equals_number)) {
            diag_add(d, kErrTestExpect, spec.path, e.line, 1, "%s.%s.%s = %g, expected %g", e.entity, e.component->name,
                     name, x, e.equals_number);
        }
        if (e.has_min && x < e.min) {
            diag_add(d, kErrTestExpect, spec.path, e.line, 1, "%s.%s.%s = %g, expected at least %g", e.entity,
                     e.component->name, name, x, e.min);
        }
        if (e.has_max && x > e.max) {
            diag_add(d, kErrTestExpect, spec.path, e.line, 1, "%s.%s.%s = %g, expected at most %g", e.entity,
                     e.component->name, name, x, e.max);
        }
    }
}

// Writes `state_hash = "<hex>"` into the [test] table: replaces the existing line or inserts one
// after the [test] header.
bool write_state_hash(const scene::TestSpec& spec, uint64_t hash) {
    FILE* f = fopen(spec.path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)size + 1);
    const size_t n = fread(text, 1, (size_t)size, f);
    text[n] = 0;
    fclose(f);
    char line_text[64];
    snprintf(line_text, sizeof(line_text), "state_hash = \"%016llx\"", (unsigned long long)hash);
    FILE* out = fopen(spec.path, "wb");
    if (!out) {
        free(text);
        return false;
    }
    int line = 1;
    bool written = false;
    const char* p = text;
    while (*p) {
        const char* eol = strchr(p, '\n');
        const size_t len = eol ? (size_t)(eol - p) + 1 : strlen(p);
        if (spec.has_state_hash && line == spec.state_hash_line) {
            fprintf(out, "%s\n", line_text);
            written = true;
        } else {
            fwrite(p, 1, len, out);
            if (!spec.has_state_hash && !written && strncmp(p, "[test]", 6) == 0) {
                if (!eol) fputc('\n', out);
                fprintf(out, "%s\n", line_text);
                written = true;
            }
        }
        p += len;
        ++line;
    }
    const bool ok = fclose(out) == 0 && written;
    free(text);
    return ok;
}

struct Outcome {
    bool passed;
    uint64_t hash;
    uint64_t ticks;
    bool updated;
};

Outcome run_one(const char* path, bool update, DiagList* d) {
    Outcome r{false, 0, 0, false};
    scene::TestSpec* spec = new scene::TestSpec();
    if (!scene::load_test_spec(path, spec, d)) {
        delete spec;
        return r;
    }
    RunOptions o;
    o.scene_path = spec->scene;
    o.sim_only = true;
    o.enemies_set = spec->has_enemies;
    o.enemies = spec->enemies;
    o.seed_set = spec->has_seed;
    o.seed = spec->seed;
    scene::Scene* s = nullptr;
    UiLayer* ui = nullptr;
    AudioLayer* audio = nullptr;
    SaveLayer* save = nullptr;
    o.save_start = spec->save[0] ? spec->save : nullptr;
    SimulateResult res;
    const uint32_t before = d->count;
    const bool ran = simulate_scene(o, spec->ticks, spec, d, &s, &ui, &res, &audio, &save);
    r.hash = res.state_hash;
    r.ticks = res.ticks;
    if (ran && s) check_expects(*spec, s, ui, audio, save, d);
    if (ran) {
        if (update && (!spec->has_state_hash || spec->state_hash != res.state_hash)) {
            r.updated = write_state_hash(*spec, res.state_hash);
            if (!r.updated) diag_add(d, kErrIo, path, 0, 0, "cannot write state_hash");
        } else if (spec->has_state_hash && spec->state_hash != res.state_hash) {
            diag_add(d, kErrTestStateHash, path, spec->state_hash_line, 1,
                     "state hash %016llx differs from the golden %016llx; if the change is intended, run "
                     "`zaro test --update`",
                     (unsigned long long)res.state_hash, (unsigned long long)spec->state_hash);
        }
    }
    r.passed = ran && d->count == before;
    destroy_ui_layer(ui);
    destroy_audio_layer(audio);
    destroy_save_layer(save);
    scene::free_scene(s);
    delete spec;
    return r;
}

}  // namespace

int run_scene_tests(const char* const* paths, int count, bool update, bool json) {
    register_all_components();
    const char* default_path = "tests";
    if (count == 0) {
        paths = &default_path;
        count = 1;
    }
    FileList* files = new FileList();
    for (int i = 0; i < count; ++i) {
        if (!platform::find_files(paths[i], ".test.toml", collect, files)) {
            log_error(kErrCliUsage, "%s: no such file or directory", paths[i]);
            delete files;
            return 2;
        }
    }
    qsort(files->paths, files->count, sizeof(char*), compare_paths);

    uint32_t passed = 0;
    DiagList* d = new DiagList();
    if (json) fputs("{\"tests\": [", stdout);
    for (uint32_t i = 0; i < files->count; ++i) {
        d->count = d->dropped = 0;
        const Outcome r = run_one(files->paths[i], update, d);
        passed += r.passed;
        if (json) {
            fputs(i ? ",\n " : "\n ", stdout);
            fputs("{\"file\": ", stdout);
            json_write_string(stdout, files->paths[i]);
            fprintf(stdout, ", \"passed\": %s, \"ticks\": %llu, \"state_hash\": \"%016llx\", \"updated\": %s, \"errors\": ",
                    r.passed ? "true" : "false", (unsigned long long)r.ticks, (unsigned long long)r.hash,
                    r.updated ? "true" : "false");
            diag_print_json(d, stdout);
            fputs("}", stdout);
        } else {
            printf("%s %s (%llu ticks, state_hash %016llx%s)\n", r.passed ? "PASS" : "FAIL", files->paths[i],
                   (unsigned long long)r.ticks, (unsigned long long)r.hash, r.updated ? ", golden updated" : "");
            diag_print_text(d, stdout);
        }
        free(files->paths[i]);
    }
    const uint32_t failed = files->count - passed;
    if (json) {
        fprintf(stdout, "%s], \"passed\": %u, \"failed\": %u}\n", files->count ? "\n" : "", passed, failed);
    } else {
        printf("zaro test: %u passed, %u failed\n", passed, failed);
    }
    const bool none = files->count == 0;
    if (none && !json) printf("zaro test: no *.test.toml files found\n");
    delete d;
    delete files;
    return failed || none ? 1 : 0;
}

}  // namespace zaro::app
