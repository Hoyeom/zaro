#include "scene/test_spec.h"

#include "core/path.h"

#include <tomlc17.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

namespace {

bool get_string(const toml_datum_t& t, const char* key, char* out, size_t size, const char* file, DiagList* d,
                bool required) {
    const toml_datum_t v = toml_get(t, key);
    if (v.type == TOML_UNKNOWN) {
        if (required) diag_add(d, kErrTestSpec, file, t.lineno, t.colno, "missing '%s'", key);
        return false;
    }
    if (v.type != TOML_STRING || (size_t)v.u.str.len >= size) {
        diag_add(d, kErrTestSpec, file, v.lineno, v.colno, "'%s' must be a string shorter than %zu bytes", key, size);
        return false;
    }
    memcpy(out, v.u.str.ptr, (size_t)v.u.str.len + 1);
    return true;
}

bool get_uint(const toml_datum_t& t, const char* key, uint64_t* out, const char* file, DiagList* d, bool required) {
    const toml_datum_t v = toml_get(t, key);
    if (v.type == TOML_UNKNOWN) {
        if (required) diag_add(d, kErrTestSpec, file, t.lineno, t.colno, "missing '%s'", key);
        return false;
    }
    if (v.type != TOML_INT64 || v.u.int64 < 0) {
        diag_add(d, kErrTestSpec, file, v.lineno, v.colno, "'%s' must be a non-negative integer", key);
        return false;
    }
    *out = (uint64_t)v.u.int64;
    return true;
}

void load_click(const toml_datum_t& t, Click* c, const char* file, DiagList* d) {
    c->line = t.lineno;
    uint64_t tick = 0;
    if (get_uint(t, "tick", &tick, file, d, true)) c->tick = (uint32_t)tick;
    get_string(t, "screen", c->screen, sizeof(c->screen), file, d, true);
    get_string(t, "node", c->node, sizeof(c->node), file, d, true);
    if (c->tick == 0 && tick == 0 && toml_get(t, "tick").type == TOML_INT64) {
        diag_add(d, kErrTestSpec, file, t.lineno, t.colno, "click ticks start at 1");
    }
}

// equals, min and max of a numeric expectation that names no field (sound, volume).
void load_number_bounds(const toml_datum_t& t, Expect* e, const char* file, DiagList* d) {
    static const char* const kKeys[] = {"equals", "min", "max"};
    for (const char* key : kKeys) {
        const toml_datum_t v = toml_get(t, key);
        if (v.type == TOML_UNKNOWN) continue;
        if (v.type != TOML_INT64 && v.type != TOML_FP64) {
            diag_add(d, kErrTestSpec, file, v.lineno, v.colno, "'%s' must be a number", key);
            continue;
        }
        const double x = v.type == TOML_INT64 ? (double)v.u.int64 : v.u.fp64;
        if (key[0] == 'e') e->has_equals = true, e->equals_number = x;
        else if (key[1] == 'i') e->has_min = true, e->min = x;
        else e->has_max = true, e->max = x;
    }
    if (!e->has_equals && !e->has_min && !e->has_max) {
        diag_add(d, kErrTestSpec, file, t.lineno, t.colno, "expect needs 'equals', 'min' or 'max'");
    }
}

void load_expect(const toml_datum_t& t, Expect* e, const char* file, DiagList* d) {
    e->line = t.lineno;
    char component[64] = {}, field[64] = {};
    // A screen node is named with `node`, a scene entity with `entity`.
    if (get_string(t, "sound", e->sound, sizeof(e->sound), file, d, false) ||
        get_string(t, "volume", e->volume, sizeof(e->volume), file, d, false)) {
        load_number_bounds(t, e, file, d);
        return;
    }
    const bool on_screen = get_string(t, "screen", e->screen, sizeof(e->screen), file, d, false);
    if (on_screen && get_string(t, "state", e->state, sizeof(e->state), file, d, false)) {
        static const char* const kStates[] = {"opening", "entering", "open", "closing", "closed"};
        bool known = false;
        for (const char* s : kStates) known = known || strcmp(s, e->state) == 0;
        if (!known) {
            diag_add(d, kErrTestSpec, file, toml_get(t, "state").lineno, 1,
                     "state must be opening, entering, open, closing or closed");
        }
        e->has_state = true;
        return;
    }
    if (on_screen && get_string(t, "text", e->text, sizeof(e->text), file, d, false)) {
        e->has_text = true;
        get_string(t, "node", e->entity, sizeof(e->entity), file, d, true);
        return;
    }
    // Read all three before returning, so one pass reports every missing key.
    const bool has_entity = get_string(t, on_screen ? "node" : "entity", e->entity, sizeof(e->entity), file, d, true);
    const bool has_component = get_string(t, "component", component, sizeof(component), file, d, true);
    const bool has_field = get_string(t, "field", field, sizeof(field), file, d, true);
    if (!has_entity || !has_component || !has_field) return;
    e->component = reflect::find_component(component);
    if (!e->component) {
        const char* hint = reflect::suggest_component(component);
        diag_add(d, kErrTestSpec, file, t.lineno, t.colno, "unknown component '%s'%s%s%s", component,
                 hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
        return;
    }
    e->field = reflect::find_field(e->component, field);
    if (!e->field) {
        const char* hint = reflect::suggest_field(e->component, field);
        diag_add(d, kErrTestSpec, file, t.lineno, t.colno, "%s has no field '%s'%s%s%s", e->component->name, field,
                 hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
        return;
    }
    const bool is_string = reflect::is_text_type(e->field->type);
    const bool is_vector = e->field->type == reflect::FieldType::kVec2 || e->field->type == reflect::FieldType::kVec3 ||
                           e->field->type == reflect::FieldType::kVec4;
    if (is_vector) {
        diag_add(d, kErrTestSpec, file, t.lineno, t.colno, "vector fields cannot be checked yet (%s.%s)",
                 e->component->name, field);
        return;
    }
    const toml_datum_t eq = toml_get(t, "equals");
    if (eq.type != TOML_UNKNOWN) {
        e->has_equals = true;
        char err[300];
        uint32_t parsed = 0;
        if (is_string && eq.type == TOML_STRING && eq.u.str.len < (int)sizeof(e->equals_string)) {
            memcpy(e->equals_string, eq.u.str.ptr, (size_t)eq.u.str.len + 1);
            // Colours and enums compare in canonical form ("#rrggbbaa", the choice's name).
            if (e->field->type != reflect::FieldType::kString) {
                if (reflect::parse_text_value(*e->field, e->equals_string, &parsed, err, sizeof(err))) {
                    reflect::format_text_value(*e->field, &parsed, e->equals_string, sizeof(e->equals_string));
                } else {
                    diag_add(d, kErrTestSpec, file, eq.lineno, eq.colno, "'equals': %s.%s %s", e->component->name,
                             field, err);
                }
            }
        } else if (!is_string && eq.type == TOML_BOOLEAN) {
            e->equals_number = eq.u.boolean ? 1.0 : 0.0;
        } else if (!is_string && (eq.type == TOML_INT64 || eq.type == TOML_FP64)) {
            e->equals_number = eq.type == TOML_INT64 ? (double)eq.u.int64 : eq.u.fp64;
        } else {
            diag_add(d, kErrTestSpec, file, eq.lineno, eq.colno, "'equals' does not match the type of %s.%s (%s)",
                     e->component->name, field, reflect::type_name(e->field->type));
        }
    }
    static const char* const kBounds[] = {"min", "max"};
    for (const char* key : kBounds) {
        const toml_datum_t v = toml_get(t, key);
        if (v.type == TOML_UNKNOWN) continue;
        if (is_string || (v.type != TOML_INT64 && v.type != TOML_FP64)) {
            diag_add(d, kErrTestSpec, file, v.lineno, v.colno, "'%s' needs a numeric field and a number", key);
            continue;
        }
        const double x = v.type == TOML_INT64 ? (double)v.u.int64 : v.u.fp64;
        if (key[1] == 'i') {
            e->has_min = true;
            e->min = x;
        } else {
            e->has_max = true;
            e->max = x;
        }
    }
    if (!e->has_equals && !e->has_min && !e->has_max) {
        diag_add(d, kErrTestSpec, file, t.lineno, t.colno, "expect needs 'equals', 'min' or 'max'");
    }
}

}  // namespace

bool load_test_spec(const char* path, TestSpec* spec, DiagList* d) {
    memset(spec, 0, sizeof(*spec));
    snprintf(spec->path, sizeof(spec->path), "%s", path);
    const uint32_t before = d->count;
    FILE* f = fopen(path, "rb");
    if (!f) {
        diag_add(d, kErrTestSpec, path, 0, 0, "cannot open test file");
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)size + 1);
    const size_t n = fread(text, 1, (size_t)size, f);
    fclose(f);
    toml_result_t r = toml_parse_named(text, (int)n, path);
    if (!r.ok) {
        diag_add(d, kErrTestSpec, path, 0, 0, "invalid TOML: %s", r.errmsg);
        toml_free(r);
        free(text);
        return false;
    }
    const toml_datum_t top = r.toptab;
    const toml_datum_t t = toml_get(top, "test");
    if (t.type != TOML_TABLE) {
        diag_add(d, kErrTestSpec, path, 1, 1, "a test file needs a [test] table");
    } else {
        char rel[260];
        if (get_string(t, "scene", rel, sizeof(rel), path, d, true)) path_join_relative(path, rel, spec->scene, sizeof(spec->scene));
        uint64_t ticks = 0;
        if (get_uint(t, "ticks", &ticks, path, d, true)) spec->ticks = (uint32_t)ticks;
        uint64_t enemies = 0;
        spec->has_enemies = get_uint(t, "enemies", &enemies, path, d, false);
        spec->enemies = (uint32_t)enemies;
        spec->has_seed = get_uint(t, "seed", &spec->seed, path, d, false);
        char hash[32];
        if (get_string(t, "state_hash", hash, sizeof(hash), path, d, false)) {
            char* end = nullptr;
            spec->state_hash = strtoull(hash, &end, 16);
            spec->has_state_hash = end && *end == 0 && hash[0];
            spec->state_hash_line = toml_get(t, "state_hash").lineno;
            if (!spec->has_state_hash) {
                diag_add(d, kErrTestSpec, path, spec->state_hash_line, 1, "state_hash must be hexadecimal");
            }
        }
    }
    const toml_datum_t clicks = toml_get(top, "click");
    if (clicks.type == TOML_ARRAY) {
        for (int32_t i = 0; i < clicks.u.arr.size && spec->click_count < TestSpec::kMaxClicks; ++i) {
            load_click(clicks.u.arr.elem[i], &spec->clicks[spec->click_count++], path, d);
        }
    }
    const toml_datum_t backs = toml_get(top, "back");
    if (backs.type == TOML_ARRAY) {
        for (int32_t i = 0; i < backs.u.arr.size && spec->click_count < TestSpec::kMaxClicks; ++i) {
            const toml_datum_t& b = backs.u.arr.elem[i];
            Click& c = spec->clicks[spec->click_count++];
            c.back = true;
            c.line = b.lineno;
            uint64_t tick = 0;
            if (get_uint(b, "tick", &tick, path, d, true)) c.tick = (uint32_t)tick;
        }
    }
    const toml_datum_t ex = toml_get(top, "expect");
    if (ex.type == TOML_ARRAY) {
        for (int32_t i = 0; i < ex.u.arr.size && spec->expect_count < TestSpec::kMaxExpects; ++i) {
            load_expect(ex.u.arr.elem[i], &spec->expects[spec->expect_count++], path, d);
        }
    }
    toml_free(r);
    free(text);
    return d->count == before;
}

}  // namespace zaro::scene
