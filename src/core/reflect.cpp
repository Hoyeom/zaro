#include "core/reflect.h"

#include "core/diag.h"

#include <stdio.h>
#include <string.h>

namespace zaro::reflect {

namespace {

constexpr uint32_t kMaxComponents = 256;
const ComponentDesc* g_components[kMaxComponents];
uint32_t g_count = 0;

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

// Case-insensitive Levenshtein distance, capped for short identifiers.
uint32_t edit_distance(const char* a, const char* b) {
    const size_t la = strlen(a), lb = strlen(b);
    if (la > 63 || lb > 63) return 99;
    uint32_t prev[64], cur[64];
    for (size_t j = 0; j <= lb; ++j) prev[j] = (uint32_t)j;
    for (size_t i = 1; i <= la; ++i) {
        cur[0] = (uint32_t)i;
        for (size_t j = 1; j <= lb; ++j) {
            const uint32_t sub = prev[j - 1] + (lower(a[i - 1]) == lower(b[j - 1]) ? 0u : 1u);
            const uint32_t del = prev[j] + 1, ins = cur[j - 1] + 1;
            cur[j] = sub < del ? (sub < ins ? sub : ins) : (del < ins ? del : ins);
        }
        memcpy(prev, cur, sizeof(uint32_t) * (lb + 1));
    }
    return prev[lb];
}

// Accept a suggestion when at most a third of the characters differ (at least 1).
const char* best(const char* name, const char* const* candidates, uint32_t count) {
    const char* found = nullptr;
    uint32_t best_d = (uint32_t)strlen(name) / 3 + 1;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t d = edit_distance(name, candidates[i]);
        if (d < best_d || (d == best_d && !found)) {
            if (d <= (uint32_t)strlen(name) / 3 + 1) {
                best_d = d;
                found = candidates[i];
            }
        }
    }
    return found;
}

}  // namespace

const char* type_name(FieldType type) {
    switch (type) {
        case FieldType::kBool: return "bool";
        case FieldType::kI32: return "i32";
        case FieldType::kU32: return "u32";
        case FieldType::kU64: return "u64";
        case FieldType::kF32: return "f32";
        case FieldType::kVec2: return "vec2";
        case FieldType::kVec3: return "vec3";
        case FieldType::kVec4: return "vec4";
        case FieldType::kString: return "string";
        case FieldType::kColor: return "color";
        case FieldType::kEnum: return "enum";
    }
    return "unknown";
}

bool is_text_type(FieldType type) {
    return type == FieldType::kString || type == FieldType::kColor || type == FieldType::kEnum;
}

namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint32_t choice_count(const FieldDesc& f) {
    uint32_t n = 0;
    while (f.choices && f.choices[n]) ++n;
    return n;
}

}  // namespace

bool parse_text_value(const FieldDesc& f, const char* text, void* dst, char* err, uint32_t err_size) {
    if (f.type == FieldType::kColor) {
        const size_t len = strlen(text);
        uint32_t bytes[4] = {0, 0, 0, 255};
        bool ok = text[0] == '#' && (len == 7 || len == 9);
        for (size_t i = 0; ok && i < (len - 1) / 2; ++i) {
            const int hi = hex_digit(text[1 + i * 2]), lo = hex_digit(text[2 + i * 2]);
            ok = hi >= 0 && lo >= 0;
            bytes[i] = (uint32_t)(hi * 16 + lo);
        }
        if (!ok) {
            snprintf(err, err_size, "expects a colour \"#RRGGBB\" or \"#RRGGBBAA\", got \"%s\"", text);
            return false;
        }
        *(uint32_t*)dst = bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24;
        return true;
    }
    if (f.type == FieldType::kEnum) {
        const uint32_t n = choice_count(f);
        for (uint32_t i = 0; i < n; ++i) {
            if (strcmp(f.choices[i], text) == 0) {
                *(uint32_t*)dst = i;
                return true;
            }
        }
        char list[256] = {};
        uint32_t used = 0;
        for (uint32_t i = 0; i < n && used < sizeof(list); ++i) {
            used += (uint32_t)snprintf(list + used, sizeof(list) - used, "%s%s", i ? ", " : "", f.choices[i]);
        }
        const char* hint = best(text, f.choices, n);
        snprintf(err, err_size, "expects one of %s, got \"%s\"%s%s%s", list, text, hint ? " (did you mean '" : "",
                 hint ? hint : "", hint ? "'?)" : "");
        return false;
    }
    snprintf(err, err_size, "is not a colour or enum field");
    return false;
}

void format_text_value(const FieldDesc& f, const void* src, char* out, uint32_t out_size) {
    if (f.type == FieldType::kColor) {
        const uint32_t c = *(const uint32_t*)src;
        snprintf(out, out_size, "#%02x%02x%02x%02x", c & 255u, (c >> 8) & 255u, (c >> 16) & 255u, c >> 24);
    } else if (f.type == FieldType::kEnum) {
        const uint32_t i = *(const uint32_t*)src;
        snprintf(out, out_size, "%s", i < choice_count(f) ? f.choices[i] : "");
    } else {
        snprintf(out, out_size, "%s", f.type == FieldType::kString ? (const char*)src : "");
    }
}

void register_component(const ComponentDesc* desc) {
    if (find_component(desc->name) || g_count >= kMaxComponents) return;
    g_components[g_count++] = desc;
}

uint32_t component_count() { return g_count; }
const ComponentDesc* component_at(uint32_t index) { return index < g_count ? g_components[index] : nullptr; }

const ComponentDesc* find_component(const char* name) {
    for (uint32_t i = 0; i < g_count; ++i) {
        if (strcmp(g_components[i]->name, name) == 0) return g_components[i];
    }
    return nullptr;
}

const FieldDesc* find_field(const ComponentDesc* desc, const char* name) {
    for (uint32_t i = 0; i < desc->field_count; ++i) {
        if (strcmp(desc->fields[i].name, name) == 0) return &desc->fields[i];
    }
    return nullptr;
}

const char* suggest_component(const char* name) {
    const char* names[kMaxComponents];
    for (uint32_t i = 0; i < g_count; ++i) names[i] = g_components[i]->name;
    return best(name, names, g_count);
}

const char* suggest_name(const char* name, const char* const* candidates, uint32_t count) {
    return best(name, candidates, count);
}

const char* suggest_field(const ComponentDesc* desc, const char* name) {
    const char* names[64];
    const uint32_t n = desc->field_count < 64 ? desc->field_count : 64;
    for (uint32_t i = 0; i < n; ++i) names[i] = desc->fields[i].name;
    return best(name, names, n);
}

namespace {

void write_default(FILE* out, const FieldDesc& f, const void* defaults) {
    const unsigned char* p = (const unsigned char*)defaults + f.offset;
    switch (f.type) {
        case FieldType::kBool: fputs(*(const bool*)p ? "true" : "false", out); break;
        case FieldType::kI32: fprintf(out, "%d", *(const int32_t*)p); break;
        case FieldType::kU32: fprintf(out, "%u", *(const uint32_t*)p); break;
        case FieldType::kU64: fprintf(out, "%llu", (unsigned long long)*(const uint64_t*)p); break;
        case FieldType::kF32: fprintf(out, "%.7g", (double)*(const float*)p); break;
        case FieldType::kVec2:
        case FieldType::kVec3:
        case FieldType::kVec4: {
            const int n = f.type == FieldType::kVec2 ? 2 : (f.type == FieldType::kVec3 ? 3 : 4);
            const float* v = (const float*)p;
            fputc('[', out);
            for (int i = 0; i < n; ++i) fprintf(out, i ? ", %.7g" : "%.7g", (double)v[i]);
            fputc(']', out);
            break;
        }
        case FieldType::kString:
        case FieldType::kColor:
        case FieldType::kEnum: {
            char text[kStringCapacity];
            format_text_value(f, p, text, sizeof(text));
            json_write_string(out, text);
            break;
        }
    }
}

}  // namespace

void write_schema_json(void* file, const SchemaList* lists, uint32_t list_count) {
    FILE* out = (FILE*)file;
    fputs("{\n  \"schema_version\": 1,\n  \"components\": [", out);
    for (uint32_t c = 0; c < g_count; ++c) {
        const ComponentDesc* d = g_components[c];
        fputs(c ? ",\n    {" : "\n    {", out);
        fputs("\"name\": ", out);
        json_write_string(out, d->name);
        fprintf(out, ", \"kind\": \"%s\", \"visual\": %s", d->kind == ComponentKind::kUi ? "ui" : "scene",
                is_visual(d) ? "true" : "false");
        fputs(", \"doc\": ", out);
        json_write_string(out, d->doc);
        fputs(", \"fields\": [", out);
        for (uint32_t i = 0; i < d->field_count; ++i) {
            const FieldDesc& f = d->fields[i];
            fputs(i ? ",\n      {" : "\n      {", out);
            fputs("\"name\": ", out);
            json_write_string(out, f.name);
            fprintf(out, ", \"type\": \"%s\", \"default\": ", type_name(f.type));
            write_default(out, f, d->defaults);
            if (f.min <= f.max) fprintf(out, ", \"min\": %.17g, \"max\": %.17g", f.min, f.max);
            if (f.type == FieldType::kEnum) {
                fputs(", \"choices\": [", out);
                for (uint32_t k = 0; f.choices && f.choices[k]; ++k) {
                    if (k) fputs(", ", out);
                    json_write_string(out, f.choices[k]);
                }
                fputc(']', out);
            }
            fputs(", \"doc\": ", out);
            json_write_string(out, f.doc);
            fputs("}", out);
        }
        fputs(d->field_count ? "\n    ]}" : "]}", out);
    }
    fputs(g_count ? "\n  ]" : "]", out);
    for (uint32_t l = 0; l < list_count; ++l) {
        fputs(",\n  ", out);
        json_write_string(out, lists[l].name);
        fputs(": [", out);
        for (uint32_t k = 0; lists[l].values[k]; ++k) {
            if (k) fputs(", ", out);
            json_write_string(out, lists[l].values[k]);
        }
        fputc(']', out);
    }
    fputs("\n}\n", out);
}

}  // namespace zaro::reflect
