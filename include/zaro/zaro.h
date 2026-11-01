/* ZARO public C API. This header is part of the plugin ABI: C only, no STL. */
#pragma once

#include <stdint.h>

#define ZARO_VERSION_MAJOR 0
#define ZARO_VERSION_MINOR 1
#define ZARO_VERSION_PATCH 0

/* Bumped only when the plugin ABI breaks. Additive changes keep it. */
#define ZARO_ABI_MAJOR 0

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zaro_version_info {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    uint32_t abi_major;
} zaro_version_info;

zaro_version_info zaro_get_version(void);

#ifdef __cplusplus
}
#endif
