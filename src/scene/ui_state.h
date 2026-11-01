// What game scripts tell the UI (P2 step 4): view-model values and commands.
//
// Values are named numbers or strings. A text shows them through placeholders: "{gold}" writes
// the value of "gold", "{gold:,}" groups the digits of a number ("1,234,567"), and "{{" is a
// literal "{". Numbers print without trailing zeros. A placeholder whose value was never set stays
// as written, so a typo shows on screen.
//
// Commands are requests that the app carries out after the script call, such as switching the
// language. The script module writes both; the ui module reads the values; the app reads the
// commands. The state belongs to the app.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace zaro::scene {

struct UiState;

UiState* create_ui_state();
void destroy_ui_state(UiState* state);

void set_value_number(UiState* state, const char* key, double value);
void set_value_string(UiState* state, const char* key, const char* value);
// False if the key was never set. A string value leaves *number 0, a number leaves *string nullptr.
bool get_value(const UiState* state, const char* key, double* number, const char** string);

// Writes text with every {placeholder} replaced. Always terminates out.
void format_text(const UiState* state, const char* text, char* out, size_t size);

enum class UiCommandKind : uint8_t {
    kLanguage,  // arg: the language to switch to
};

struct UiCommand {
    UiCommandKind kind;
    char arg[64];
};

void push_command(UiState* state, UiCommandKind kind, const char* arg);
uint32_t command_count(const UiState* state);
const UiCommand* command_at(const UiState* state, uint32_t index);
void clear_commands(UiState* state);

}  // namespace zaro::scene
