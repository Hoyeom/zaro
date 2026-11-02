#include "app/save_layer.h"

#include "core/log.h"
#include "platform/platform.h"
#include "scene/save_spec.h"
#include "scene/scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

namespace {

// --- The file store: save.toml in a directory, the previous copy in save.toml.bak.

struct FileStore {
    char path[700];
    char backup[720];
    bool locked;  // a newer game's save is there: never overwrite it
    bool warned;
};

scene::SaveRead read_file_into(const char* path, const scene::SaveSpec* spec, scene::SaveData* data, bool* found,
                               DiagList* d) {
    uint64_t size = 0;
    char* text = platform::read_file(path, &size);
    *found = text != nullptr;
    if (!text) return scene::SaveRead::kDamaged;
    uint32_t version = 0;
    const scene::SaveRead r = scene::read_save_text(spec, text, (size_t)size, path, data, &version, d);
    free(text);
    return r;
}

scene::SaveRead file_load(void* user, const scene::SaveSpec* spec, scene::SaveData* data, DiagList* d) {
    FileStore* s = (FileStore*)user;
    bool found = false;
    DiagList* main_problems = new DiagList();
    scene::SaveRead r = read_file_into(s->path, spec, data, &found, main_problems);
    if (found && r != scene::SaveRead::kDamaged) {
        for (uint32_t i = 0; i < main_problems->count; ++i) {
            const Diag& p = main_problems->items[i];
            diag_add(d, p.id, p.file, p.line, p.column, "%s", p.message);
        }
    } else {
        // No save.toml (a write was cut short between its renames) or a damaged one: the backup.
        bool backup_found = false;
        DiagList* backup_problems = new DiagList();
        const scene::SaveRead b = read_file_into(s->backup, spec, data, &backup_found, backup_problems);
        if (backup_found && b != scene::SaveRead::kDamaged) {
            if (found) {
                diag_add(d, kErrSaveFile, s->path, 0, 0, "the save is damaged (%s); loaded the backup %s",
                         main_problems->count ? main_problems->items[0].message : "unreadable", s->backup);
            }
            r = b;
        } else if (found) {
            for (uint32_t i = 0; i < main_problems->count; ++i) {
                const Diag& p = main_problems->items[i];
                diag_add(d, p.id, p.file, p.line, p.column, "%s", p.message);
            }
            scene::reset_save_data(data, spec);
        } else {
            r = scene::SaveRead::kLoaded;  // a new game: the defaults
            scene::reset_save_data(data, spec);
        }
        delete backup_problems;
    }
    delete main_problems;
    s->locked = r == scene::SaveRead::kNewer;
    return r;
}

bool file_commit(void* user, const scene::SaveData* data, DiagList* d) {
    FileStore* s = (FileStore*)user;
    if (s->locked) {
        if (!s->warned) {
            diag_add(d, kErrSaveNewer, s->path, 0, 0,
                     "not saving: the save there is from a newer version of the game and is kept as it is");
        }
        s->warned = true;
        return false;
    }
    size_t size = 0;
    char* text = scene::write_save_text(data, &size);
    const bool ok = platform::write_file_atomic(s->path, text, size, s->backup);
    free(text);
    if (!ok) diag_add(d, kErrSaveWrite, s->path, 0, 0, "cannot write the save (disk full or no permission?)");
    return ok;
}

void file_destroy(void* user) { delete (FileStore*)user; }

// --- The memory store: the last commit's text.

struct MemoryStore {
    char start[260];
    char* text;
};

scene::SaveRead memory_load(void* user, const scene::SaveSpec* spec, scene::SaveData* data, DiagList* d) {
    MemoryStore* s = (MemoryStore*)user;
    scene::reset_save_data(data, spec);
    if (!s->start[0]) return scene::SaveRead::kLoaded;
    bool found = false;
    const scene::SaveRead r = read_file_into(s->start, spec, data, &found, d);
    if (!found) diag_add(d, kErrSaveFile, s->start, 0, 0, "cannot open the save file");
    return r;
}

bool memory_commit(void* user, const scene::SaveData* data, DiagList*) {
    MemoryStore* s = (MemoryStore*)user;
    free(s->text);
    size_t size = 0;
    s->text = scene::write_save_text(data, &size);
    return true;
}

void memory_destroy(void* user) {
    MemoryStore* s = (MemoryStore*)user;
    free(s->text);
    delete s;
}

}  // namespace

SaveStore file_save_store(const char* dir) {
    FileStore* s = new FileStore();
    snprintf(s->path, sizeof(s->path), "%ssave.toml", dir);
    snprintf(s->backup, sizeof(s->backup), "%ssave.toml.bak", dir);
    return SaveStore{s, file_load, file_commit, file_destroy};
}

SaveStore memory_save_store(const char* start) {
    MemoryStore* s = new MemoryStore();
    snprintf(s->start, sizeof(s->start), "%s", start ? start : "");
    return SaveStore{s, memory_load, memory_commit, memory_destroy};
}

struct SaveLayer {
    scene::SaveSpec* spec = nullptr;
    scene::SaveData* data = nullptr;       // what scripts read and write
    scene::SaveData* committed = nullptr;  // what the store last took
    SaveStore store = {};
    scene::SaveRead read = {};
    char path[720] = {};
};

namespace {

void print_and_clear(DiagList* d) {
    diag_print_text(d, stderr);
    d->count = d->dropped = 0;
}

}  // namespace

SaveLayer* create_save_layer(const scene::Scene* scene, const char* save_dir, bool in_memory, const char* start,
                             DiagList* diags, bool* failed) {
    *failed = false;
    if (!scene->save[0]) return nullptr;
    scene::SaveSpec* spec = scene::load_save_spec(scene->save, diags);
    if (!spec) {
        *failed = true;
        return nullptr;
    }
    SaveLayer* l = new SaveLayer();
    l->spec = spec;
    l->data = new scene::SaveData();
    l->committed = new scene::SaveData();
    char dir[600] = {};
    if (save_dir) {
        const size_t n = strlen(save_dir);
        snprintf(dir, sizeof(dir), "%s%s", save_dir, n && save_dir[n - 1] != '/' && save_dir[n - 1] != '\\' ? "/" : "");
        platform::create_directories(dir);
    } else if (!in_memory && !platform::user_data_dir(spec->game, dir, sizeof(dir))) {
        log_info("no user data directory for '%s'; the save is kept in memory only", spec->game);
        in_memory = true;
    }
    if (save_dir || !in_memory) {
        l->store = file_save_store(dir);
        snprintf(l->path, sizeof(l->path), "%ssave.toml", dir);
    } else {
        l->store = memory_save_store(start);
    }
    // A test's starting save must load cleanly; a player's save never stops the game.
    const bool strict = in_memory && start && !save_dir;
    DiagList* problems = strict ? diags : new DiagList();
    const uint32_t before = problems->count;
    l->read = l->store.load(l->store.user, spec, l->data, problems);
    if (!strict) {
        print_and_clear(problems);
        delete problems;
    } else if (diags->count != before) {
        destroy_save_layer(l);
        *failed = true;
        return nullptr;
    }
    *l->committed = *l->data;
    l->data->commit_requested = false;
    return l;
}

void destroy_save_layer(SaveLayer* l) {
    if (!l) return;
    flush_save(l);
    if (l->store.destroy) l->store.destroy(l->store.user);
    delete l->committed;
    delete l->data;
    scene::free_save_spec(l->spec);
    delete l;
}

scene::SaveData* save_data(SaveLayer* l) { return l ? l->data : nullptr; }

void flush_save(SaveLayer* l) {
    if (!l || !l->data->commit_requested) return;
    l->data->commit_requested = false;
    DiagList* d = new DiagList();
    if (l->store.commit(l->store.user, l->data, d)) *l->committed = *l->data;
    print_and_clear(d);
    delete d;
}

scene::SaveRead save_read(const SaveLayer* l) { return l->read; }

bool committed_value(const SaveLayer* l, const char* field, double* number, const char** string) {
    const int f = l ? scene::find_save_field(l->spec, field) : -1;
    if (f < 0) return false;
    const scene::SaveValue& v = l->committed->values[f];
    *string = nullptr;
    *number = 0.0;
    switch (v.type) {
        case scene::kSaveInt: *number = (double)v.i; break;
        case scene::kSaveNumber: *number = v.n; break;
        case scene::kSaveBool: *number = v.b ? 1.0 : 0.0; break;
        case scene::kSaveString: *string = v.s; break;
    }
    return true;
}

const char* save_path(const SaveLayer* l) { return l ? l->path : ""; }

bool check_scene_save(const scene::Scene* s, DiagList* diags) {
    if (!s->save[0]) return true;
    scene::SaveSpec* spec = scene::load_save_spec(s->save, diags);
    scene::free_save_spec(spec);
    return spec != nullptr;
}

}  // namespace zaro::app
