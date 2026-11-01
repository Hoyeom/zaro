#include "core/log.h"

#include <stdarg.h>
#include <stdio.h>

namespace zaro {

void log_error(ErrorId id, const char* fmt, ...) {
    fprintf(stderr, "zaro: error[ZE%04d]: ", (int)id);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

void log_info(const char* fmt, ...) {
    fputs("zaro: ", stderr);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

}  // namespace zaro
