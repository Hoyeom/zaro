#include "core/diag.h"

#include <stdarg.h>
#include <string.h>

namespace zaro {

void diag_add(DiagList* list, ErrorId id, const char* file, int line, int column, const char* fmt, ...) {
    if (list->count >= DiagList::kMax) {
        list->dropped++;
        return;
    }
    Diag& d = list->items[list->count++];
    d.id = id;
    d.line = line;
    d.column = column;
    snprintf(d.file, sizeof(d.file), "%s", file ? file : "");
    va_list args;
    va_start(args, fmt);
    vsnprintf(d.message, sizeof(d.message), fmt, args);
    va_end(args);
}

void diag_print_text(const DiagList* list, FILE* out) {
    for (uint32_t i = 0; i < list->count; ++i) {
        const Diag& d = list->items[i];
        if (d.line > 0) {
            fprintf(out, "%s:%d:%d: error[ZE%04d]: %s\n", d.file, d.line, d.column, (int)d.id, d.message);
        } else {
            fprintf(out, "%s: error[ZE%04d]: %s\n", d.file, (int)d.id, d.message);
        }
    }
    if (list->dropped) fprintf(out, "zaro: %u more error(s) not shown\n", list->dropped);
}

void json_write_string(FILE* out, const char* s) {
    fputc('"', out);
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        switch (*p) {
            case '"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (*p < 0x20) fprintf(out, "\\u%04x", *p);
                else fputc(*p, out);
        }
    }
    fputc('"', out);
}

void diag_print_json(const DiagList* list, FILE* out) {
    fputs("[", out);
    for (uint32_t i = 0; i < list->count; ++i) {
        const Diag& d = list->items[i];
        fputs(i ? ",\n " : "\n ", out);
        fprintf(out, "{\"id\": \"ZE%04d\", \"file\": ", (int)d.id);
        json_write_string(out, d.file);
        fprintf(out, ", \"line\": %d, \"column\": %d, \"message\": ", d.line, d.column);
        json_write_string(out, d.message);
        fputs("}", out);
    }
    fputs(list->count ? "\n]\n" : "]\n", out);
}

}  // namespace zaro
