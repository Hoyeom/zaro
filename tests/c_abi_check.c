/* Compiled as C so that a C++-only construct in a public header fails the build. */
#include "zaro/zaro.h"

int zaro_c_abi_check(void) {
    zaro_version_info v = zaro_get_version();
    return (int)v.abi_major;
}
