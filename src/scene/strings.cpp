#include "scene/strings.h"

#include <tomlc17.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

namespace {

struct Builder {
    StringTable* table;
    uint32_t capacity = 0;
    const char* path;
    DiagList* diags;
};

void add_entry(Builder& b, const char* key, const toml_datum_t& v) {
    if (v.type != TOML_STRING) {
        diag_add(b.diags, kErrStringTable, b.path, v.lineno, v.colno, "'%s' must be a string", key);
        return;
    }
    if (strlen(key) >= sizeof(StringEntry::key)) {
        diag_add(b.diags, kErrStringTable, b.path, v.lineno, v.colno, "key '%s' is longer than %zu bytes", key,
                 sizeof(StringEntry::key) - 1);
        return;
    }
    StringTable* t = b.table;
    if (t->count == b.capacity) {
        b.capacity = b.capacity ? b.capacity * 2 : 64;
        t->entries = (StringEntry*)realloc(t->entries, sizeof(StringEntry) * b.capacity);
    }
    StringEntry& e = t->entries[t->count++];
    snprintf(e.key, sizeof(e.key), "%s", key);
    e.value = (char*)malloc((size_t)v.u.str.len + 1);
    memcpy(e.value, v.u.str.ptr, (size_t)v.u.str.len + 1);
    e.line = v.lineno;
}

void add_table(Builder& b, const char* prefix, const toml_datum_t& table) {
    for (int32_t i = 0; i < table.u.tab.size; ++i) {
        char key[256];
        snprintf(key, sizeof(key), "%s%s%s", prefix, prefix[0] ? "." : "", table.u.tab.key[i]);
        const toml_datum_t& v = table.u.tab.value[i];
        if (v.type == TOML_TABLE) add_table(b, key, v);
        else add_entry(b, key, v);
    }
}

int compare_entries(const void* a, const void* b) {
    return strcmp(((const StringEntry*)a)->key, ((const StringEntry*)b)->key);
}

// Closest key of the table, for "did you mean".
const char* suggest_key(const StringTable* t, const char* key) {
    const char** names = (const char**)malloc(sizeof(char*) * (t->count ? t->count : 1));
    for (uint32_t i = 0; i < t->count; ++i) names[i] = t->entries[i].key;
    const char* hint = reflect::suggest_name(key, names, t->count);
    free(names);
    return hint;
}

}  // namespace

StringTable* load_string_table(const char* dir, const char* language, DiagList* diags) {
    StringTable* t = (StringTable*)calloc(1, sizeof(StringTable));
    snprintf(t->path, sizeof(t->path), "%s%s.toml", dir, language);
    snprintf(t->language, sizeof(t->language), "%s", language);
    FILE* f = fopen(t->path, "rb");
    if (!f) {
        diag_add(diags, kErrStringTable, t->path, 0, 0, "cannot open string table for language '%s'", language);
        free_string_table(t);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)size + 1);
    const size_t n = fread(text, 1, (size_t)size, f);
    text[n] = 0;
    fclose(f);
    toml_result_t r = toml_parse_named(text, (int)n, t->path);
    const uint32_t errors_before = diags->count;
    if (!r.ok) {
        diag_add(diags, kErrStringTable, t->path, 0, 0, "invalid TOML: %s", r.errmsg);
    } else {
        Builder b{t, 0, t->path, diags};
        add_table(b, "", r.toptab);
        qsort(t->entries, t->count, sizeof(StringEntry), compare_entries);
    }
    if (r.ok || r.__internal) toml_free(r);
    free(text);
    if (diags->count != errors_before) {
        free_string_table(t);
        return nullptr;
    }
    return t;
}

void free_string_table(StringTable* t) {
    if (!t) return;
    for (uint32_t i = 0; i < t->count; ++i) free(t->entries[i].value);
    free(t->entries);
    free(t);
}

const char* find_string(const StringTable* t, const char* key) {
    StringEntry probe;
    snprintf(probe.key, sizeof(probe.key), "%s", key);
    const StringEntry* e = (const StringEntry*)bsearch(&probe, t->entries, t->count, sizeof(StringEntry), compare_entries);
    return e ? e->value : nullptr;
}

void check_string_keys(const Scene* screen, const StringTable* const* tables, uint32_t table_count, DiagList* diags) {
    for (uint32_t i = 0; i < screen->entity_count; ++i) {
        const Entity& e = screen->entities[i];
        for (uint32_t c = 0; c < e.component_count; ++c) {
            const ComponentData& cd = e.components[c];
            for (uint32_t f = 0; f < cd.desc->field_count; ++f) {
                const reflect::FieldDesc& field = cd.desc->fields[f];
                const char* text = (const char*)cd.data + field.offset;
                if (field.type != reflect::FieldType::kString || text[0] != '@') continue;
                const int line = cd.field_lines[f] ? cd.field_lines[f] : e.line;
                if (!table_count) {
                    diag_add(diags, kErrStringKey, screen->path, line, 1,
                             "%s.%s: '%s' is a string key, but the scene has no strings and languages", cd.desc->name,
                             field.name, text);
                }
                for (uint32_t t = 0; t < table_count; ++t) {
                    if (find_string(tables[t], text + 1)) continue;
                    const char* hint = suggest_key(tables[t], text + 1);
                    diag_add(diags, kErrStringKey, screen->path, line, 1, "%s.%s: '%s' has no entry in %s%s%s%s",
                             cd.desc->name, field.name, text + 1, tables[t]->path, hint ? " (did you mean '@" : "",
                             hint ? hint : "", hint ? "'?)" : "");
                }
            }
        }
    }
}

void check_tables_agree(const StringTable* const* tables, uint32_t table_count, DiagList* diags) {
    for (uint32_t t = 1; t < table_count; ++t) {
        for (uint32_t i = 0; i < tables[0]->count; ++i) {
            const StringEntry& e = tables[0]->entries[i];
            if (!find_string(tables[t], e.key)) {
                diag_add(diags, kErrStringTable, tables[t]->path, 0, 0, "missing '%s' (%s has it on line %d)", e.key,
                         tables[0]->path, e.line);
            }
        }
        for (uint32_t i = 0; i < tables[t]->count; ++i) {
            const StringEntry& e = tables[t]->entries[i];
            if (!find_string(tables[0], e.key)) {
                diag_add(diags, kErrStringTable, tables[t]->path, e.line, 1, "'%s' is not in %s, the default language",
                         e.key, tables[0]->path);
            }
        }
    }
}

}  // namespace zaro::scene
