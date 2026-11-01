// Collected diagnostics with file, line and column. Tools report every problem in one pass,
// as text (`path:line:col: error[ZEnnnn]: message`) or as JSON for agents (`--json`).
#pragma once

#include "core/log.h"

#include <stdint.h>
#include <stdio.h>

namespace zaro {

struct Diag {
    ErrorId id;
    int line;    // 1-based, 0 when unknown
    int column;  // 1-based, 0 when unknown
    char file[260];
    char message[400];
};

struct DiagList {
    static constexpr uint32_t kMax = 256;
    Diag items[kMax];
    uint32_t count = 0;
    uint32_t dropped = 0;  // diagnostics past kMax
};

void diag_add(DiagList* list, ErrorId id, const char* file, int line, int column, const char* fmt, ...);
void diag_print_text(const DiagList* list, FILE* out);
void diag_print_json(const DiagList* list, FILE* out);

// Writes s as a JSON string literal (with quotes).
void json_write_string(FILE* out, const char* s);

}  // namespace zaro
