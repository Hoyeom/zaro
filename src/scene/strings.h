// String tables for localized UI text: one TOML file per language, <strings dir>/<language>.toml.
//
//   [hud]
//   gold = "골드"                 # key "hud.gold"; nested tables join their names with '.'
//   [settings]
//   title = "설정"
//
// Texts in screen files that start with '@' name a key ("@hud.gold"). check_string_keys reports
// keys that a table lacks, and tables that lack keys another language has.
#pragma once

#include "core/diag.h"
#include "scene/scene.h"

#include <stdint.h>

namespace zaro::scene {

struct StringEntry {
    char key[128];
    char* value;  // UTF-8, owned by the table
    int line;
};

struct StringTable {
    char path[kPathCapacity];
    char language[kLanguageCapacity];
    StringEntry* entries;  // sorted by key
    uint32_t count;
};

// Loads <dir><language>.toml. Values must be strings. Problems are ZE0703 diagnostics.
StringTable* load_string_table(const char* dir, const char* language, DiagList* diags);
void free_string_table(StringTable* table);

// The value for a key, or nullptr.
const char* find_string(const StringTable* table, const char* key);

// Checks every '@key' string field of the screen's widgets against each table (ZE0702, with a
// "did you mean" hint), and that every table has the keys of the first one (ZE0703).
void check_string_keys(const Scene* screen, const StringTable* const* tables, uint32_t table_count, DiagList* diags);
void check_tables_agree(const StringTable* const* tables, uint32_t table_count, DiagList* diags);

}  // namespace zaro::scene
