// Component reflection: field names, types, offsets, defaults and ranges.
//
// Components are plain structs with default member initializers. A module describes each one
// with ZARO_FIELD / ZARO_COMPONENT and registers it at startup; the scene loader, the schema
// export (`zaro schema`) and future tools work from these descriptions only.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace zaro::reflect {

// kColor and kEnum are stored as uint32_t and written as text (TOML strings, Luau strings):
//   kColor: sRGB RGBA8 with red in the low byte, written "#RRGGBB" or "#RRGGBBAA"
//   kEnum:  index into FieldDesc::choices, written as the choice's name
enum class FieldType : uint8_t { kBool, kI32, kU32, kU64, kF32, kVec2, kVec3, kVec4, kString, kColor, kEnum };

// Fixed-size UTF-8 string field, so components stay plain data.
constexpr uint32_t kStringCapacity = 128;

struct FieldDesc {
    const char* name;
    FieldType type;
    uint32_t offset;
    const char* doc;
    double min;  // inclusive; min > max means unbounded (numeric types only)
    double max;
    const char* const* choices;  // kEnum: names, nullptr-terminated; nullptr otherwise
};

struct ComponentDesc {
    const char* name;
    const char* doc;
    uint32_t size;
    const void* defaults;  // an instance holding the default values
    const FieldDesc* fields;
    uint32_t field_count;
};

const char* type_name(FieldType type);

// True for the types written as strings: kString, kColor and kEnum.
bool is_text_type(FieldType type);
// Parses a kColor or kEnum value. On failure writes the reason (with a "did you mean" hint for
// enums) to err and returns false.
bool parse_text_value(const FieldDesc& field, const char* text, void* dst, char* err, uint32_t err_size);
// Writes a kString, kColor or kEnum value as text ("#rrggbbaa" for colours).
void format_text_value(const FieldDesc& field, const void* src, char* out, uint32_t out_size);

// Registry. Registering the same name twice keeps the first registration.
void register_component(const ComponentDesc* desc);
uint32_t component_count();
const ComponentDesc* component_at(uint32_t index);
const ComponentDesc* find_component(const char* name);
const FieldDesc* find_field(const ComponentDesc* desc, const char* name);

// Closest registered name within a small edit distance, or nullptr. Used for "did you mean".
const char* suggest_component(const char* name);
const char* suggest_field(const ComponentDesc* desc, const char* name);

// Every registered component with its fields, types, defaults, ranges and docs, as JSON.
// This is what `zaro schema` prints; agents read it to write scenes.
void write_schema_json(void* file /* FILE* */);

}  // namespace zaro::reflect

#define ZARO_FIELD_RANGE(Struct, member, type, doc, min, max) \
    { #member, zaro::reflect::FieldType::type, (uint32_t)offsetof(Struct, member), doc, min, max, nullptr }
#define ZARO_FIELD(Struct, member, type, doc) ZARO_FIELD_RANGE(Struct, member, type, doc, 1.0, 0.0)
// A uint32_t member holding an index into `choices` (const char* const[], nullptr-terminated).
#define ZARO_FIELD_ENUM(Struct, member, doc, choices) \
    { #member, zaro::reflect::FieldType::kEnum, (uint32_t)offsetof(Struct, member), doc, 1.0, 0.0, choices }

// Defines `const zaro::reflect::ComponentDesc k<Struct>Desc` from an array of ZARO_FIELDs.
#define ZARO_COMPONENT(Struct, doc, fields_array)                                                    \
    static const Struct k##Struct##Defaults{};                                                      \
    const zaro::reflect::ComponentDesc k##Struct##Desc = {#Struct, doc, (uint32_t)sizeof(Struct),   \
                                                          &k##Struct##Defaults, fields_array,       \
                                                          (uint32_t)(sizeof(fields_array) /        \
                                                                     sizeof(fields_array[0]))}
