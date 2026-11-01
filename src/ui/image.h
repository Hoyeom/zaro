// PNG decoding through stb_image. image.cpp is the only file that includes stb_image.h.
// Internal to the ui module.
#pragma once

#include <stdint.h>

namespace zaro::ui {

// Decodes a PNG file to straight-alpha RGBA8. Free the pixels with free_image. False if the file
// is missing or not a PNG.
bool decode_png(const char* path, uint8_t** rgba, uint32_t* width, uint32_t* height);
void free_image(uint8_t* rgba);

}  // namespace zaro::ui
