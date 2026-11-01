#include "core/path.h"

#include <stdio.h>
#include <string.h>

namespace zaro {

namespace {

bool is_sep(char c) { return c == '/' || c == '\\'; }
bool is_absolute(const char* p) { return is_sep(p[0]) || (p[0] && p[1] == ':'); }

}  // namespace

void path_join_relative(const char* base_file, const char* rel, char* out, size_t size) {
    char joined[1024];
    if (is_absolute(rel)) {
        snprintf(joined, sizeof(joined), "%s", rel);
    } else {
        const char* last = nullptr;
        for (const char* p = base_file; *p; ++p) {
            if (is_sep(*p)) last = p;
        }
        if (last) snprintf(joined, sizeof(joined), "%.*s/%s", (int)(last - base_file), base_file, rel);
        else snprintf(joined, sizeof(joined), "%s", rel);
    }

    // Split into segments and collapse "." and "..".
    const char* segs[128];
    size_t lens[128];
    size_t count = 0;
    const char* p = joined;
    const bool rooted = is_sep(p[0]);
    while (*p) {
        while (is_sep(*p)) ++p;
        const char* start = p;
        while (*p && !is_sep(*p)) ++p;
        const size_t len = (size_t)(p - start);
        if (len == 0 || (len == 1 && start[0] == '.')) continue;
        const bool dotdot = len == 2 && start[0] == '.' && start[1] == '.';
        const bool can_pop = count > 0 && !(lens[count - 1] == 2 && segs[count - 1][0] == '.' && segs[count - 1][1] == '.') &&
                             !(lens[count - 1] == 2 && segs[count - 1][1] == ':');
        if (dotdot && can_pop) {
            --count;
        } else if (count < 128) {
            segs[count] = start;
            lens[count] = len;
            ++count;
        }
    }
    size_t o = 0;
    if (rooted && o + 1 < size) out[o++] = '/';
    for (size_t i = 0; i < count; ++i) {
        if (i && o + 1 < size) out[o++] = '/';
        for (size_t j = 0; j < lens[i] && o + 1 < size; ++j) out[o++] = segs[i][j];
    }
    if (count == 0 && !rooted && o + 1 < size) out[o++] = '.';
    out[o] = 0;
}

}  // namespace zaro
