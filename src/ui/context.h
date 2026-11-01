// UI resources shared by every screen: fonts by name, the glyph atlas and the image atlas.
#pragma once

#include "core/diag.h"
#include "scene/strings.h"
#include "ui/text.h"

#include <stdint.h>

namespace zaro::ui {

struct Context;

struct ContextDesc {
    uint32_t glyph_atlas_size = kDefaultAtlasSize;
    uint32_t image_atlas_size = 2048;
};

Context* create_context(const ContextDesc& desc = ContextDesc{});
void destroy_context(Context* context);
TextSystem* context_text(Context* context);

// Loads a font and gives it a name that widgets use ("regular", "bold"). Returns false after a
// ZE0700 diagnostic.
bool add_font(Context* context, const char* name, const char* path, DiagList* diags);
// Font id for a name; -1 if no font has that name.
int find_font(const Context* context, const char* name);

// Loads a PNG into the image atlas, once per path. Returns the image id, or -1 after a ZE0701
// diagnostic (missing, not a PNG, or no room left in the atlas). Images stay until the context goes.
int load_image(Context* context, const char* path, DiagList* diags);

struct ImageInfo {
    uint32_t width = 0, height = 0;  // in pixels
    float uv[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};
ImageInfo image_info(const Context* context, int image);

// The string table that '@key' texts resolve against (nullptr: none). Not owned.
void set_strings(Context* context, const scene::StringTable* table);
// The text to show: the table's value for "@key", or the text itself when it has no '@' or the key
// is missing (so a missing key shows as "@key").
const char* resolve_text(const Context* context, const char* text);

// Call once per frame before drawing.
void begin_frame(Context* context);

// The image atlas (RGBA8 sRGB, straight alpha; `size` squared texels, 4 bytes each) and the region
// changed since the last call, which is then marked clean.
AtlasUpdate take_image_update(Context* context);

}  // namespace zaro::ui
