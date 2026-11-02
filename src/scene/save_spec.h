// Local saves (D-033). A game declares what it saves in a save declaration that the scene names
// with `save = "save.toml"` in [scene]; scripts read and write only those fields (script/script.h).
//
//   [save]
//   game = "neon"              # required: the game's id, also its folder on the device: a-z 0-9 . _ -
//   version = 2                # required, 1 or more: raise it when the fields change
//
//   [[field]]
//   name = "best_score"        # required, unique: letters, digits and _
//   type = "int"               # required: int, number, bool or string (strings up to 255 bytes)
//   default = 0                # optional: 0, false or "" when not given
//   min = 0                    # optional, int and number: the allowed range
//   max = 1000000000
//   merge = "max"              # optional: how a server save (P4c) merges two copies: latest (the
//                              # default), max or min (int and number only)
//   local = true               # optional: the value belongs to this device (volumes) and never
//                              # leaves it
//
//   [[migrate]]                # turns a save written with version `from` into version from + 1
//   from = 1
//   rename = { best = "best_score" }  # old name = new name
//   remove = ["tutorial_seen"]
//   set = { coins = 100 }      # a value, whatever the old save had
//
// A save file is TOML too, written by the engine:
//
//   [save]
//   game = "neon"
//   version = 2
//   [data]
//   best_score = 1200
//
// Reading an older file runs every [[migrate]] step from its version up, in order of `from`; fields
// the file does not have then get their defaults, and keys no field declares are dropped. Values
// of the wrong type or out of range are reported and take the default.
#pragma once

#include "core/diag.h"

#include <stddef.h>
#include <stdint.h>

namespace zaro::scene {

constexpr uint32_t kMaxSaveFields = 128;
constexpr uint32_t kMaxSaveMigrations = 64;
constexpr uint32_t kMaxSaveOps = 512;
constexpr uint32_t kSaveStringCapacity = 256;

enum SaveType : uint8_t { kSaveInt, kSaveNumber, kSaveBool, kSaveString };
enum SaveMerge : uint8_t { kMergeLatest, kMergeMax, kMergeMin };

struct SaveValue {
    uint8_t type;  // SaveType
    int64_t i;
    double n;
    bool b;
    char s[kSaveStringCapacity];
};

struct SaveField {
    char name[64];
    uint8_t type;  // SaveType
    SaveValue def;
    bool has_min, has_max;
    double min, max;
    uint8_t merge;  // SaveMerge
    bool local;
    int line;
};

struct SaveOp {
    enum Kind : uint8_t { kRename, kRemove, kSet };
    Kind kind;
    char key[64];
    char to[64];     // kRename
    SaveValue value;  // kSet
};

struct SaveMigration {
    uint32_t from;
    uint32_t first_op, op_count;  // into SaveSpec::ops
    int line;
};

struct SaveSpec {
    char path[260];
    char game[64];
    uint32_t version;
    SaveField fields[kMaxSaveFields];
    uint32_t field_count;
    SaveMigration migrations[kMaxSaveMigrations];  // in order of `from`
    uint32_t migration_count;
    SaveOp ops[kMaxSaveOps];
    uint32_t op_count;
};

// The values of a save while a game runs: one per field of the spec.
struct SaveData {
    const SaveSpec* spec;
    SaveValue values[kMaxSaveFields];
    uint32_t changes;        // bumped by every change of a value
    bool commit_requested;   // save.commit() since the store last took the values
};

// Loads and checks a save declaration. Every problem goes to diags (ZE1350); nullptr if any.
SaveSpec* load_save_spec(const char* path, DiagList* diags);
void free_save_spec(SaveSpec* spec);

int find_save_field(const SaveSpec* spec, const char* name);
const char* suggest_save_field(const SaveSpec* spec, const char* name);
const char* save_type_name(uint8_t type);

// The spec's defaults.
void reset_save_data(SaveData* data, const SaveSpec* spec);
// Checks a value against the field's type and range (an int may be given as a whole number) and
// stores it. False with a message in err.
bool set_save_value(SaveData* data, uint32_t field, const SaveValue& value, char* err, size_t err_size);

enum class SaveRead : uint8_t {
    kLoaded,    // the file was written with the spec's version
    kMigrated,  // an older version, migrated
    kNewer,     // written by a newer version of the game: not loaded (ZE1352), data keeps the defaults
    kDamaged,   // not a save of this game (ZE1351), data keeps the defaults
};

// Reads a save file's text into data (which starts from the defaults). Problems with single
// values are ZE1351 diagnostics at the file's lines; the rest of the save still loads.
SaveRead read_save_text(const SaveSpec* spec, const char* text, size_t size, const char* path, SaveData* data,
                        uint32_t* file_version, DiagList* diags);
// The save file's text for data, allocated with malloc and NUL-terminated.
char* write_save_text(const SaveData* data, size_t* size);

}  // namespace zaro::scene
