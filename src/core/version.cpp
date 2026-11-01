#include "zaro/zaro.h"

extern "C" zaro_version_info zaro_get_version(void) {
    return {ZARO_VERSION_MAJOR, ZARO_VERSION_MINOR, ZARO_VERSION_PATCH, ZARO_ABI_MAJOR};
}
