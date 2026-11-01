// PNG files: a minimal writer (uncompressed deflate) for screenshots and cooked atlases, and a
// reader for images, sprite sheets and SVG art (stb_image, in png_decode.cpp).
#pragma once

#include <stdint.h>

namespace zaro {

// Writes 8-bit RGBA pixels, rows top to bottom. Returns false if the file cannot be written.
bool write_png_rgba8(const char* path, uint32_t width, uint32_t height, const uint8_t* rgba);

// Decodes a PNG file to straight-alpha RGBA8, rows top to bottom. Free the pixels with free_png.
// False if the file is missing or not a PNG.
bool read_png_rgba8(const char* path, uint8_t** rgba, uint32_t* width, uint32_t* height);
void free_png(uint8_t* rgba);
// Reads only the size from a PNG file's header. False if the file is missing or not a PNG.
bool png_size(const char* path, uint32_t* width, uint32_t* height);

}  // namespace zaro
