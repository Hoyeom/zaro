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
    }
    return "unknown";
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
        case FieldType::kString: json_write_string(out, (const char*)p); break;
    }
}

}  // namespace

void write_schema_json(void* file) {
    FILE* out = (FILE*)file;
    fputs("{\n  \"schema_version\": 1,\n  \"components\": [", out);
    for (uint32_t c = 0; c < g_count; ++c) {
        const ComponentDesc* d = g_components[c];
        fputs(c ? ",\n    {" : "\n    {", out);
        fputs("\"name\": ", out);
        json_write_string(out, d->name);
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
            fputs(", \"doc\": ", out);
            json_write_string(out, f.doc);
            fputs("}", out);
        }
        fputs(d->field_count ? "\n    ]}" : "]}", out);
    }
    fputs(g_count ? "\n  ]\n}\n" : "]\n}\n", out);
}

}  // namespace zaro::reflect
