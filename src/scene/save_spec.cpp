#include "scene/save_spec.h"

#include "core/reflect.h"

#include <tomlc17.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

namespace {

const char* const kTypeNames[] = {"int", "number", "bool", "string", nullptr};
const char* const kMergeNames[] = {"latest", "max", "min", nullptr};

bool is_toml_number(const toml_datum_t& v) { return v.type == TOML_INT64 || v.type == TOML_FP64; }
double toml_number(const toml_datum_t& v) { return v.type == TOML_INT64 ? (double)v.u.int64 : v.u.fp64; }

int choice(const toml_datum_t& v, const char* const* names) {
    for (int i = 0; v.type == TOML_STRING && names[i]; ++i) {
        if (strcmp(v.u.str.ptr, names[i]) == 0) return i;
    }
    return -1;
}

bool valid_name(const char* s, bool id) {
    if (!s[0]) return false;
    for (const char* p = s; *p; ++p) {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || (!id && c >= 'A' && c <= 'Z') ||
                        (id && (c == '-' || c == '.'));
        if (!ok) return false;
    }
    return true;
}

// A TOML scalar as a value of its own type; false for tables, arrays and dates.
bool to_value(const toml_datum_t& v, SaveValue* out) {
    *out = SaveValue{};
    switch (v.type) {
        case TOML_INT64: out->type = kSaveInt, out->i = v.u.int64; return true;
        case TOML_FP64: out->type = kSaveNumber, out->n = v.u.fp64; return true;
        case TOML_BOOLEAN: out->type = kSaveBool, out->b = v.u.boolean; return true;
        case TOML_STRING:
            if ((size_t)v.u.str.len >= kSaveStringCapacity) return false;
            out->type = kSaveString;
            memcpy(out->s, v.u.str.ptr, (size_t)v.u.str.len + 1);
            return true;
        default: return false;
    }
}

// "is below its minimum 0", "is above its maximum 1", or "" when x is in the field's range.
const char* range_problem(const SaveField& f, double x, char* out, size_t size) {
    out[0] = 0;
    if (f.has_min && x < f.min) snprintf(out, size, "is below its minimum %g", f.min);
    else if (f.has_max && x > f.max) snprintf(out, size, "is above its maximum %g", f.max);
    return out;
}

// Converts a value to a field's type and checks its range. False with a message in err.
bool fit_value(const SaveField& f, const SaveValue& in, SaveValue* out, char* err, size_t err_size) {
    *out = SaveValue{};
    out->type = f.type;
    double x = 0.0;
    char range[64];
    switch (f.type) {
        case kSaveInt:
            if (in.type == kSaveInt) out->i = in.i;
            else if (in.type == kSaveNumber && in.n == floor(in.n) && fabs(in.n) < 9.0e15) out->i = (int64_t)in.n;
            else break;
            x = (double)out->i;
            if (range_problem(f, x, range, sizeof(range))[0]) {
                snprintf(err, err_size, "%s = %lld %s", f.name, (long long)out->i, range);
                return false;
            }
            return true;
        case kSaveNumber:
            if (in.type == kSaveNumber) out->n = in.n;
            else if (in.type == kSaveInt) out->n = (double)in.i;
            else break;
            if (!isfinite(out->n)) {
                snprintf(err, err_size, "%s must be a finite number", f.name);
                return false;
            }
            if (range_problem(f, out->n, range, sizeof(range))[0]) {
                snprintf(err, err_size, "%s = %g %s", f.name, out->n, range);
                return false;
            }
            return true;
        case kSaveBool:
            if (in.type != kSaveBool) break;
            out->b = in.b;
            return true;
        case kSaveString:
            if (in.type != kSaveString) break;
            memcpy(out->s, in.s, sizeof(out->s));
            return true;
    }
    snprintf(err, err_size, "%s must be %s %s, got %s", f.name, f.type == kSaveInt ? "an" : "a", save_type_name(f.type),
             save_type_name(in.type));
    return false;
}

bool read_text(const char* path, char** text, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *text = (char*)malloc((size_t)(n > 0 ? n : 0) + 1);
    *size = fread(*text, 1, (size_t)(n > 0 ? n : 0), f);
    (*text)[*size] = 0;
    fclose(f);
    return true;
}

void load_field(SaveSpec* s, const toml_datum_t& t, DiagList* d) {
    if (s->field_count == kMaxSaveFields) {
        diag_add(d, kErrSaveSpec, s->path, t.lineno, t.colno, "more than %u fields", kMaxSaveFields);
        return;
    }
    SaveField f = {};
    f.line = t.lineno;
    const toml_datum_t name = toml_get(t, "name"), type = toml_get(t, "type");
    if (name.type != TOML_STRING || !valid_name(name.u.str.ptr, false) || strlen(name.u.str.ptr) >= sizeof(f.name)) {
        diag_add(d, kErrSaveSpec, s->path, t.lineno, t.colno,
                 "[[field]] needs a name of letters, digits and _ (under 64 bytes)");
        return;
    }
    memcpy(f.name, name.u.str.ptr, strlen(name.u.str.ptr) + 1);
    const int ty = choice(type, kTypeNames);
    if (ty < 0) {
        diag_add(d, kErrSaveSpec, s->path, type.type ? type.lineno : t.lineno, type.type ? type.colno : t.colno,
                 "field '%s' needs type = \"int\", \"number\", \"bool\" or \"string\"", f.name);
        return;
    }
    f.type = (uint8_t)ty;
    f.def.type = f.type;
    bool ok = true;
    toml_datum_t def = {};
    for (int32_t k = 0; k < t.u.tab.size; ++k) {
        const char* key = t.u.tab.key[k];
        const toml_datum_t& v = t.u.tab.value[k];
        const bool numeric = f.type == kSaveInt || f.type == kSaveNumber;
        if (strcmp(key, "name") == 0 || strcmp(key, "type") == 0) {
        } else if (strcmp(key, "default") == 0) {
            def = v;
        } else if ((strcmp(key, "min") == 0 || strcmp(key, "max") == 0) && numeric && is_toml_number(v)) {
            (key[1] == 'i' ? f.has_min : f.has_max) = true;
            (key[1] == 'i' ? f.min : f.max) = toml_number(v);
        } else if (strcmp(key, "min") == 0 || strcmp(key, "max") == 0) {
            diag_add(d, kErrSaveSpec, s->path, v.lineno, v.colno, "%s needs an int or number field and a number", key);
            ok = false;
        } else if (strcmp(key, "merge") == 0) {
            const int m = choice(v, kMergeNames);
            if (m < 0 || (m != kMergeLatest && !numeric)) {
                diag_add(d, kErrSaveSpec, s->path, v.lineno, v.colno,
                         "merge must be latest, or max or min for int and number fields");
                ok = false;
            }
            f.merge = (uint8_t)(m < 0 ? 0 : m);
        } else if (strcmp(key, "local") == 0 && v.type == TOML_BOOLEAN) {
            f.local = v.u.boolean;
        } else {
            diag_add(d, kErrSaveSpec, s->path, v.lineno, v.colno,
                     "unexpected key '%s' in [[field]] (expected name, type, default, min, max, merge, local)", key);
            ok = false;
        }
    }
    if (f.has_min && f.has_max && f.min > f.max) {
        diag_add(d, kErrSaveSpec, s->path, t.lineno, t.colno, "field '%s': min is above max", f.name);
        ok = false;
    }
    if (def.type != TOML_UNKNOWN) {
        SaveValue in;
        char err[200];
        if (!to_value(def, &in) || !fit_value(f, in, &f.def, err, sizeof(err))) {
            if (!to_value(def, &in)) snprintf(err, sizeof(err), "default of %s must be a %s", f.name, save_type_name(f.type));
            diag_add(d, kErrSaveSpec, s->path, def.lineno, def.colno, "%s", err);
            ok = false;
        }
    } else if (f.type == kSaveInt || f.type == kSaveNumber) {
        // Without a default, 0, or the range's nearest end.
        const double z = f.has_min && f.min > 0.0 ? f.min : (f.has_max && f.max < 0.0 ? f.max : 0.0);
        if (f.type == kSaveInt) f.def.i = (int64_t)ceil(z);
        else f.def.n = z;
    }
    if (find_save_field(s, f.name) >= 0) {
        diag_add(d, kErrSaveSpec, s->path, t.lineno, t.colno, "field '%s' is declared twice", f.name);
        ok = false;
    }
    if (ok) s->fields[s->field_count++] = f;
}

bool add_op(SaveSpec* s, const SaveOp& op, const toml_datum_t& at, DiagList* d) {
    if (s->op_count == kMaxSaveOps) {
        diag_add(d, kErrSaveSpec, s->path, at.lineno, at.colno, "more than %u migration changes", kMaxSaveOps);
        return false;
    }
    s->ops[s->op_count++] = op;
    return true;
}

bool copy_key(char* out, const char* key, const toml_datum_t& at, const char* path, DiagList* d) {
    if (!key[0] || strlen(key) >= 64) {
        diag_add(d, kErrSaveSpec, path, at.lineno, at.colno, "a migrated key must be a name under 64 bytes");
        return false;
    }
    memcpy(out, key, strlen(key) + 1);
    return true;
}

void load_migration(SaveSpec* s, const toml_datum_t& t, DiagList* d) {
    if (s->migration_count == kMaxSaveMigrations) {
        diag_add(d, kErrSaveSpec, s->path, t.lineno, t.colno, "more than %u [[migrate]] steps", kMaxSaveMigrations);
        return;
    }
    SaveMigration m = {};
    m.line = t.lineno;
    m.first_op = s->op_count;
    const toml_datum_t from = toml_get(t, "from");
    if (from.type != TOML_INT64 || from.u.int64 < 1 || (s->version && from.u.int64 >= (int64_t)s->version)) {
        diag_add(d, kErrSaveSpec, s->path, from.type ? from.lineno : t.lineno, 1,
                 "[[migrate]] needs from = a version from 1 to %u (one below the save's version)",
                 s->version > 1 ? s->version - 1 : 1);
        return;
    }
    m.from = (uint32_t)from.u.int64;
    for (uint32_t i = 0; i < s->migration_count; ++i) {
        if (s->migrations[i].from == m.from) {
            diag_add(d, kErrSaveSpec, s->path, t.lineno, t.colno, "two [[migrate]] steps from version %u", m.from);
            return;
        }
    }
    bool ok = true;
    for (int32_t k = 0; k < t.u.tab.size; ++k) {
        const char* key = t.u.tab.key[k];
        const toml_datum_t& v = t.u.tab.value[k];
        if (strcmp(key, "from") == 0) continue;
        if (strcmp(key, "rename") == 0 && v.type == TOML_TABLE) {
            for (int32_t i = 0; i < v.u.tab.size && ok; ++i) {
                SaveOp op = {};
                op.kind = SaveOp::kRename;
                const toml_datum_t& to = v.u.tab.value[i];
                if (to.type != TOML_STRING) {
                    diag_add(d, kErrSaveSpec, s->path, to.lineno, to.colno, "rename maps an old name to a new name string");
                    ok = false;
                    break;
                }
                ok = copy_key(op.key, v.u.tab.key[i], to, s->path, d) && copy_key(op.to, to.u.str.ptr, to, s->path, d) &&
                     add_op(s, op, to, d);
            }
        } else if (strcmp(key, "remove") == 0 && v.type == TOML_ARRAY) {
            for (int32_t i = 0; i < v.u.arr.size && ok; ++i) {
                SaveOp op = {};
                op.kind = SaveOp::kRemove;
                const toml_datum_t& e = v.u.arr.elem[i];
                if (e.type != TOML_STRING) {
                    diag_add(d, kErrSaveSpec, s->path, v.lineno, v.colno, "remove is an array of names");
                    ok = false;
                    break;
                }
                ok = copy_key(op.key, e.u.str.ptr, v, s->path, d) && add_op(s, op, v, d);
            }
        } else if (strcmp(key, "set") == 0 && v.type == TOML_TABLE) {
            for (int32_t i = 0; i < v.u.tab.size && ok; ++i) {
                SaveOp op = {};
                op.kind = SaveOp::kSet;
                const toml_datum_t& x = v.u.tab.value[i];
                if (!to_value(x, &op.value)) {
                    diag_add(d, kErrSaveSpec, s->path, x.lineno, x.colno, "set takes an int, number, bool or string");
                    ok = false;
                    break;
                }
                ok = copy_key(op.key, v.u.tab.key[i], x, s->path, d) && add_op(s, op, x, d);
            }
        } else {
            diag_add(d, kErrSaveSpec, s->path, v.lineno, v.colno,
                     "unexpected key '%s' in [[migrate]] (expected from, rename = {..}, remove = [..], set = {..})", key);
            ok = false;
        }
    }
    if (!ok) {
        s->op_count = m.first_op;
        return;
    }
    m.op_count = s->op_count - m.first_op;
    // Kept in order of `from`.
    uint32_t at = s->migration_count;
    while (at > 0 && s->migrations[at - 1].from > m.from) {
        s->migrations[at] = s->migrations[at - 1];
        --at;
    }
    s->migrations[at] = m;
    ++s->migration_count;
}

// What a save file holds while it is migrated: its keys and their values, in file order.
struct Entry {
    char key[64];
    SaveValue value;
    int line;
    bool removed;
};

Entry* find_entry(Entry* entries, uint32_t count, const char* key) {
    for (uint32_t i = 0; i < count; ++i) {
        if (!entries[i].removed && strcmp(entries[i].key, key) == 0) return &entries[i];
    }
    return nullptr;
}

void write_escaped(char* out, size_t* at, size_t cap, const char* s) {
    for (const char* p = s; *p && *at + 8 < cap; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            out[(*at)++] = '\\';
            out[(*at)++] = (char)c;
        } else if (c < 0x20 || c == 0x7f) {
            *at += (size_t)snprintf(out + *at, cap - *at, "\\u%04x", c);
        } else {
            out[(*at)++] = (char)c;
        }
    }
}

}  // namespace

const char* save_type_name(uint8_t type) { return type < 4 ? kTypeNames[type] : "?"; }

SaveSpec* load_save_spec(const char* path, DiagList* d) {
    char* text = nullptr;
    size_t n = 0;
    if (!read_text(path, &text, &n)) {
        diag_add(d, kErrSaveSpec, path, 0, 0, "cannot open save declaration");
        return nullptr;
    }
    SaveSpec* s = (SaveSpec*)calloc(1, sizeof(SaveSpec));
    snprintf(s->path, sizeof(s->path), "%s", path);
    const uint32_t before = d->count;
    toml_result_t r = toml_parse_named(text, (int)n, path);
    if (!r.ok) {
        diag_add(d, kErrSceneSyntax, path, 0, 0, "invalid TOML: %s", r.errmsg);
    } else {
        const toml_datum_t top = r.toptab;
        for (int32_t i = 0; i < top.u.tab.size; ++i) {
            const char* key = top.u.tab.key[i];
            const toml_datum_t& v = top.u.tab.value[i];
            const bool known = (strcmp(key, "save") == 0 && v.type == TOML_TABLE) ||
                               ((strcmp(key, "field") == 0 || strcmp(key, "migrate") == 0) && v.type == TOML_ARRAY);
            if (!known) {
                diag_add(d, kErrSaveSpec, path, v.lineno, v.colno,
                         "unexpected top-level key '%s' (expected [save], [[field]] and [[migrate]])", key);
            }
        }
        const toml_datum_t h = toml_get(top, "save");
        if (h.type != TOML_TABLE) {
            diag_add(d, kErrSaveSpec, path, 1, 1, "a save declaration needs a [save] table");
        } else {
            for (int32_t i = 0; i < h.u.tab.size; ++i) {
                const char* key = h.u.tab.key[i];
                const toml_datum_t& v = h.u.tab.value[i];
                if (strcmp(key, "game") == 0) {
                    if (v.type != TOML_STRING || !valid_name(v.u.str.ptr, true) || strlen(v.u.str.ptr) >= sizeof(s->game)) {
                        diag_add(d, kErrSaveSpec, path, v.lineno, v.colno,
                                 "game must be an id of a-z, 0-9, '.', '_' and '-' (under 64 bytes)");
                    } else {
                        memcpy(s->game, v.u.str.ptr, strlen(v.u.str.ptr) + 1);
                    }
                } else if (strcmp(key, "version") == 0) {
                    if (v.type != TOML_INT64 || v.u.int64 < 1 || v.u.int64 > 1000000) {
                        diag_add(d, kErrSaveSpec, path, v.lineno, v.colno, "version must be an integer from 1");
                    } else {
                        s->version = (uint32_t)v.u.int64;
                    }
                } else {
                    diag_add(d, kErrSaveSpec, path, v.lineno, v.colno,
                             "unexpected key '%s' in [save] (expected game, version)", key);
                }
            }
            if (!s->game[0] || !s->version) {
                if (d->count == before) {
                    diag_add(d, kErrSaveSpec, path, h.lineno, h.colno, "[save] needs game = \"<id>\" and version = <n>");
                }
            }
        }
        const toml_datum_t fields = toml_get(top, "field");
        for (int32_t i = 0; fields.type == TOML_ARRAY && i < fields.u.arr.size; ++i) {
            if (fields.u.arr.elem[i].type == TOML_TABLE) load_field(s, fields.u.arr.elem[i], d);
        }
        const toml_datum_t migrations = toml_get(top, "migrate");
        for (int32_t i = 0; migrations.type == TOML_ARRAY && i < migrations.u.arr.size; ++i) {
            if (migrations.u.arr.elem[i].type == TOML_TABLE) load_migration(s, migrations.u.arr.elem[i], d);
        }
    }
    if (r.ok || r.__internal) toml_free(r);
    free(text);
    if (d->count != before) {
        free(s);
        return nullptr;
    }
    return s;
}

void free_save_spec(SaveSpec* s) { free(s); }

int find_save_field(const SaveSpec* s, const char* name) {
    for (uint32_t i = 0; i < s->field_count; ++i) {
        if (strcmp(s->fields[i].name, name) == 0) return (int)i;
    }
    return -1;
}

const char* suggest_save_field(const SaveSpec* s, const char* name) {
    const char* names[kMaxSaveFields];
    for (uint32_t i = 0; i < s->field_count; ++i) names[i] = s->fields[i].name;
    return reflect::suggest_name(name, names, s->field_count);
}

void reset_save_data(SaveData* data, const SaveSpec* spec) {
    data->spec = spec;
    for (uint32_t i = 0; i < spec->field_count; ++i) data->values[i] = spec->fields[i].def;
    ++data->changes;
}

bool set_save_value(SaveData* data, uint32_t field, const SaveValue& value, char* err, size_t err_size) {
    if (field >= data->spec->field_count) return false;
    SaveValue fitted;
    if (!fit_value(data->spec->fields[field], value, &fitted, err, err_size)) return false;
    data->values[field] = fitted;
    ++data->changes;
    return true;
}

SaveRead read_save_text(const SaveSpec* spec, const char* text, size_t size, const char* path, SaveData* data,
                        uint32_t* file_version, DiagList* d) {
    reset_save_data(data, spec);
    *file_version = 0;
    toml_result_t r = toml_parse_named(text, (int)size, path);
    if (!r.ok) {
        diag_add(d, kErrSaveFile, path, 0, 0, "the save is damaged (invalid TOML: %s)", r.errmsg);
        if (r.__internal) toml_free(r);
        return SaveRead::kDamaged;
    }
    const toml_datum_t top = r.toptab;
    const toml_datum_t h = toml_get(top, "save"), body = toml_get(top, "data");
    const toml_datum_t game = h.type == TOML_TABLE ? toml_get(h, "game") : toml_datum_t{};
    const toml_datum_t version = h.type == TOML_TABLE ? toml_get(h, "version") : toml_datum_t{};
    if (game.type != TOML_STRING || version.type != TOML_INT64 || version.u.int64 < 1 ||
        (body.type != TOML_TABLE && body.type != TOML_UNKNOWN)) {
        diag_add(d, kErrSaveFile, path, 1, 1, "the save is damaged: it needs [save] game and version, and [data]");
        toml_free(r);
        return SaveRead::kDamaged;
    }
    if (strcmp(game.u.str.ptr, spec->game) != 0) {
        diag_add(d, kErrSaveFile, path, game.lineno, game.colno, "the save belongs to game '%s', not '%s'",
                 game.u.str.ptr, spec->game);
        toml_free(r);
        return SaveRead::kDamaged;
    }
    *file_version = version.u.int64 > 1000000 ? 1000000u : (uint32_t)version.u.int64;
    if (*file_version > spec->version) {
        diag_add(d, kErrSaveNewer, path, version.lineno, version.colno,
                 "the save was written by version %u of the game, newer than this one (%u); it is not loaded and "
                 "will not be overwritten", *file_version, spec->version);
        toml_free(r);
        return SaveRead::kNewer;
    }

    // The file's keys, then the migration steps from its version up.
    const uint32_t cap = (body.type == TOML_TABLE ? (uint32_t)body.u.tab.size : 0) + spec->op_count + 1;
    Entry* entries = (Entry*)calloc(cap, sizeof(Entry));
    uint32_t count = 0;
    for (int32_t i = 0; body.type == TOML_TABLE && i < body.u.tab.size; ++i) {
        const toml_datum_t& v = body.u.tab.value[i];
        Entry& e = entries[count];
        if (strlen(body.u.tab.key[i]) >= sizeof(e.key) || !to_value(v, &e.value)) {
            diag_add(d, kErrSaveFile, path, v.lineno, v.colno, "'%s' is not a save value; it is dropped", body.u.tab.key[i]);
            continue;
        }
        memcpy(e.key, body.u.tab.key[i], strlen(body.u.tab.key[i]) + 1);
        e.line = v.lineno;
        ++count;
    }
    for (uint32_t m = 0; m < spec->migration_count; ++m) {
        const SaveMigration& step = spec->migrations[m];
        if (step.from < *file_version) continue;
        for (uint32_t o = 0; o < step.op_count; ++o) {
            const SaveOp& op = spec->ops[step.first_op + o];
            Entry* e = find_entry(entries, count, op.key);
            if (op.kind == SaveOp::kRename && e) {
                if (Entry* old = find_entry(entries, count, op.to)) old->removed = true;
                memcpy(e->key, op.to, sizeof(e->key));
            } else if (op.kind == SaveOp::kRemove && e) {
                e->removed = true;
            } else if (op.kind == SaveOp::kSet) {
                if (!e) {
                    e = &entries[count++];
                    memcpy(e->key, op.key, sizeof(e->key));
                }
                e->value = op.value;
                e->line = 0;
            }
        }
    }
    // Into the declared fields; what no field declares is dropped.
    for (uint32_t i = 0; i < count; ++i) {
        const Entry& e = entries[i];
        if (e.removed) continue;
        const int f = find_save_field(spec, e.key);
        char err[300];
        if (f < 0) {
            const char* hint = suggest_save_field(spec, e.key);
            diag_add(d, kErrSaveFile, path, e.line, e.line ? 1 : 0,
                     "no field '%s' is declared%s%s%s; it is dropped (a [[migrate]] step can rename or remove it)",
                     e.key, hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
        } else if (!set_save_value(data, (uint32_t)f, e.value, err, sizeof(err))) {
            diag_add(d, kErrSaveFile, path, e.line, e.line ? 1 : 0, "%s; the default is used", err);
        }
    }
    free(entries);
    toml_free(r);
    return *file_version < spec->version ? SaveRead::kMigrated : SaveRead::kLoaded;
}

char* write_save_text(const SaveData* data, size_t* size) {
    const SaveSpec* spec = data->spec;
    const size_t cap = 512 + (size_t)spec->field_count * (64 + 2 * kSaveStringCapacity + 32);
    char* out = (char*)malloc(cap);
    size_t at = (size_t)snprintf(out, cap, "# Saved by zaro (D-033).\n[save]\ngame = \"%s\"\nversion = %u\n\n[data]\n",
                                 spec->game, spec->version);
    for (uint32_t i = 0; i < spec->field_count; ++i) {
        const SaveValue& v = data->values[i];
        at += (size_t)snprintf(out + at, cap - at, "%s = ", spec->fields[i].name);
        switch (v.type) {
            case kSaveInt: at += (size_t)snprintf(out + at, cap - at, "%lld\n", (long long)v.i); break;
            case kSaveNumber: {
                // Shortest text that reads back to the same double, always with a '.' or exponent.
                char num[40];
                for (int digits = 15; digits <= 17; ++digits) {
                    snprintf(num, sizeof(num), "%.*g", digits, v.n);
                    if (strtod(num, nullptr) == v.n) break;
                }
                if (!strpbrk(num, ".eEn")) strcat(num, ".0");
                at += (size_t)snprintf(out + at, cap - at, "%s\n", num);
                break;
            }
            case kSaveBool: at += (size_t)snprintf(out + at, cap - at, "%s\n", v.b ? "true" : "false"); break;
            case kSaveString:
                out[at++] = '"';
                write_escaped(out, &at, cap, v.s);
                at += (size_t)snprintf(out + at, cap - at, "\"\n");
                break;
        }
    }
    out[at] = 0;
    *size = at;
    return out;
}

}  // namespace zaro::scene
