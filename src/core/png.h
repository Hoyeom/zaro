// Minimal PNG writer (uncompressed deflate). Used for `zaro run --screenshot`.
#pragma once

#include <stdint.h>

namespace zaro {

// Writes 8-bit RGBA pixels, rows top to bottom. Returns false if the file cannot be written.
bool write_png_rgba8(const char* path, uint32_t width, uint32_t height, const uint8_t* rgba);

}  // namespace zaro
