#include "scene/ui_state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

namespace {

constexpr uint32_t kMaxCommands = 64;
constexpr size_t kKeyCapacity = 64;

}  // namespace

struct UiValue {
    char key[kKeyCapacity];
    double number;
    char* string;  // nullptr for numbers
};

struct UiState {
    UiValue* values = nullptr;
    uint32_t count = 0;
    uint32_t capacity = 0;
    UiCommand commands[kMaxCommands];
    uint32_t command_count = 0;
};

namespace {

UiValue* find(const UiState* s, const char* key, size_t len) {
    for (uint32_t i = 0; i < s->count; ++i) {
        if (strlen(s->values[i].key) == len && strncmp(s->values[i].key, key, len) == 0) return &s->values[i];
    }
    return nullptr;
}

UiValue* find_or_add(UiState* s, const char* key) {
    if (UiValue* v = find(s, key, strlen(key))) return v;
    if (s->count == s->capacity) {
        s->capacity = s->capacity ? s->capacity * 2 : 32;
        s->values = (UiValue*)realloc(s->values, sizeof(UiValue) * s->capacity);
    }
    UiValue& v = s->values[s->count++];
    snprintf(v.key, sizeof(v.key), "%s", key);
    v.number = 0.0;
    v.string = nullptr;
    return &v;
}

// Integers print as integers, other numbers with up to 6 significant digits; `group` adds commas.
void format_number(double x, bool group, char* out, size_t size) {
    char digits[64];
    if (fabs(x) < 1e15 && x == floor(x)) snprintf(digits, sizeof(digits), "%.0f", x);
    else snprintf(digits, sizeof(digits), "%.6g", x);
    if (!group) {
        snprintf(out, size, "%s", digits);
        return;
    }
    const char* p = digits;
    size_t o = 0;
    if (*p == '-' && o + 1 < size) out[o++] = *p++;
    size_t int_len = 0;
    while (p[int_len] >= '0' && p[int_len] <= '9') ++int_len;
    for (size_t i = 0; i < int_len && o + 1 < size; ++i) {
        if (i && (int_len - i) % 3 == 0 && o + 2 < size) out[o++] = ',';
        out[o++] = p[i];
    }
    for (const char* q = p + int_len; *q && o + 1 < size; ++q) out[o++] = *q;
    out[o] = 0;
}

}  // namespace

UiState* create_ui_state() { return new UiState(); }

void destroy_ui_state(UiState* s) {
    if (!s) return;
    for (uint32_t i = 0; i < s->count; ++i) free(s->values[i].string);
    free(s->values);
    delete s;
}

void set_value_number(UiState* s, const char* key, double value) {
    UiValue* v = find_or_add(s, key);
    free(v->string);
    v->string = nullptr;
    v->number = value;
}

void set_value_string(UiState* s, const char* key, const char* value) {
    UiValue* v = find_or_add(s, key);
    const size_t len = strlen(value);
    char* copy = (char*)malloc(len + 1);
    memcpy(copy, value, len + 1);
    free(v->string);
    v->string = copy;
    v->number = 0.0;
}

bool get_value(const UiState* s, const char* key, double* number, const char** string) {
    const UiValue* v = find(s, key, strlen(key));
    if (!v) return false;
    *number = v->number;
    *string = v->string;
    return true;
}

void format_text(const UiState* s, const char* text, char* out, size_t size) {
    size_t o = 0;
    const char* p = text;
    while (*p && o + 1 < size) {
        if (p[0] == '{' && p[1] == '{') {
            out[o++] = '{';
            p += 2;
            continue;
        }
        const char* close = p[0] == '{' ? strchr(p, '}') : nullptr;
        if (!close) {
            out[o++] = *p++;
            continue;
        }
        const char* name = p + 1;
        const char* colon = (const char*)memchr(name, ':', (size_t)(close - name));
        const size_t name_len = (size_t)((colon ? colon : close) - name);
        const bool group = colon && close - colon == 2 && colon[1] == ',';
        const UiValue* v = s ? find(s, name, name_len) : nullptr;
        if (!v) {  // unknown: keep the placeholder as written
            out[o++] = *p++;
            continue;
        }
        char value[128];
        if (v->string) snprintf(value, sizeof(value), "%s", v->string);
        else format_number(v->number, group, value, sizeof(value));
        for (const char* q = value; *q && o + 1 < size; ++q) out[o++] = *q;
        p = close + 1;
    }
    out[o] = 0;
}

void push_command(UiState* s, UiCommandKind kind, const char* arg) {
    if (s->command_count == kMaxCommands) return;
    UiCommand& c = s->commands[s->command_count++];
    c.kind = kind;
    snprintf(c.arg, sizeof(c.arg), "%s", arg);
}

uint32_t command_count(const UiState* s) { return s->command_count; }
const UiCommand* command_at(const UiState* s, uint32_t i) { return i < s->command_count ? &s->commands[i] : nullptr; }
void clear_commands(UiState* s) { s->command_count = 0; }

}  // namespace zaro::scene
