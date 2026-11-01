#include "ui/image.h"

// stb_image is third-party C written for other warning levels; compile it quietly.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4244 4245 4456 4457 4701 4702 4703)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_FAILURE_USERMSG
#include "stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

namespace zaro::ui {

bool decode_png(const char* path, uint8_t** rgba, uint32_t* width, uint32_t* height) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path, &w, &h, &channels, 4);
    if (!pixels) return false;
    *rgba = pixels;
    *width = (uint32_t)w;
    *height = (uint32_t)h;
    return true;
}

void free_image(uint8_t* rgba) { stbi_image_free(rgba); }

}  // namespace zaro::ui
