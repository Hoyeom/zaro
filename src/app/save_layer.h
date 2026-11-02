// The game's local save (D-033): its declaration (scene/save_spec.h), the values scripts read and
// write (scene::SaveData), and the store that keeps what they commit. The values are loaded before
// the script's on_start; a save.commit() hands them to the store after that tick or frame.
//
// A store is where committed saves go. The built-in ones keep them in a file in a directory (a
// temporary file renamed over save.toml, the previous copy kept as save.toml.bak) or in memory.
// The backend plugin (P4c) provides another store with its own authorities and merge; game files
// and scripts stay the same.
#pragma once

#include "core/diag.h"

#include <stdint.h>

namespace zaro::scene {
struct Scene;
struct SaveSpec;
struct SaveData;
enum class SaveRead : uint8_t;
}  // namespace zaro::scene

namespace zaro::app {

struct SaveStore {
    void* user;
    // Fills data (which holds the defaults) with the stored save, migrated. Problems are
    // diagnostics; a damaged or newer save leaves the defaults.
    scene::SaveRead (*load)(void* user, const scene::SaveSpec* spec, scene::SaveData* data, DiagList* diags);
    // Keeps the values. False with a diagnostic when they could not be kept.
    bool (*commit)(void* user, const scene::SaveData* data, DiagList* diags);
    void (*destroy)(void* user);
};

// Keeps save.toml in `dir` (with a trailing separator).
SaveStore file_save_store(const char* dir);
// Keeps the last commit in memory, starting from the save file `start` (nullptr: none).
SaveStore memory_save_store(const char* start);

struct SaveLayer;

// Loads the scene's save declaration and the stored save. Where it is stored: `save_dir` when
// given, else in memory with in_memory (headless runs, scene tests; `start` is then a save file to
// begin from), else the game's user data directory. Returns nullptr when the scene declares no
// save, or after adding diagnostics (*failed is then true). Problems with a player's stored save
// (damaged, newer, a value that does not fit) are printed and the game goes on with what loaded;
// problems with a test's starting save are diagnostics and fail.
SaveLayer* create_save_layer(const scene::Scene* scene, const char* save_dir, bool in_memory, const char* start,
                             DiagList* diags, bool* failed);
// Commits what is pending, then frees the store.
void destroy_save_layer(SaveLayer* layer);

// What `save.*` reads and writes (pass it in script::ScriptDesc).
scene::SaveData* save_data(SaveLayer* layer);
// Hands the values to the store if a script asked for a commit since the last flush. A failed
// write is printed (ZE1353); the game goes on and the next commit tries again.
void flush_save(SaveLayer* layer);
// How the stored save was read when the layer was created.
scene::SaveRead save_read(const SaveLayer* layer);
// The value of a field as last committed: a number (bools 0 or 1) or, for a string field, *string.
// False for no such field. Scene tests check what a commit wrote, not uncommitted changes.
bool committed_value(const SaveLayer* layer, const char* field, double* number, const char** string);
// The file a file store writes, or "" for memory.
const char* save_path(const SaveLayer* layer);

// `zaro check`: the scene's save declaration. Returns false if anything was reported.
bool check_scene_save(const scene::Scene* scene, DiagList* diags);

}  // namespace zaro::app
