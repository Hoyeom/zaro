#include "app/ui_layer.h"

#include "core/log.h"
#include "core/path.h"
#include "core/profile.h"
#include "render/ui_renderer.h"
#include "scene/scene.h"
#include "scene/strings.h"
#include "scene/ui_state.h"
#include "ui/context.h"
#include "ui/draw.h"
#include "ui/screen.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

namespace zaro::app {

static_assert(sizeof(ui::Quad) == sizeof(render::UiQuad), "ui::Quad and render::UiQuad must match");

struct UiLayer {
    ui::Context* context = nullptr;
    scene::Scene* docs[scene::kMaxSceneScreens] = {};
    ui::Screen* screens[scene::kMaxSceneScreens] = {};
    uint32_t count = 0;
    scene::StringTable* strings = nullptr;
    ui::DrawList list;
    scene::Scene* scene = nullptr;
    scene::UiState* state = nullptr;
    rhi::Device* device = nullptr;
    render::UiRenderer* renderer = nullptr;
    float width = 0.0f, height = 0.0f;
    bool back_used = false;
};

namespace {

bool load_fonts(ui::Context* context, DiagList* diags) {
    char path[600];
    snprintf(path, sizeof(path), "%sfonts/NotoSansKR-Regular.otf", platform::base_path());
    const bool regular = ui::add_font(context, "regular", path, diags);
    snprintf(path, sizeof(path), "%sfonts/NotoSansKR-Bold.otf", platform::base_path());
    return ui::add_font(context, "bold", path, diags) && regular;
}

ui::UiInput to_ui_input(const platform::Input& in) {
    ui::UiInput u;
    u.pointer_x = in.pointer_x;
    u.pointer_y = in.pointer_y;
    u.pointer_down = in.pointer_down;
    u.pointer_pressed = in.pointer_pressed;
    u.pointer_released = in.pointer_released;
    u.pointer_touch = in.pointer_touch;
    u.wheel_y = in.wheel_y;
    u.nav_up = in.nav_up;
    u.nav_down = in.nav_down;
    u.nav_left = in.nav_left;
    u.nav_right = in.nav_right;
    u.nav_confirm = in.nav_confirm;
    u.nav_back = in.nav_back;
    return u;
}

}  // namespace

UiLayer* create_ui_layer(scene::Scene* scene, const char* language, DiagList* diags, bool* failed) {
    *failed = false;
    if (!scene || !scene->ui_count) return nullptr;
    const uint32_t errors_before = diags->count;
    UiLayer* l = new UiLayer();
    l->scene = scene;
    l->state = scene::create_ui_state();
    l->context = ui::create_context();
    ui::set_ui_state(l->context, l->state);
    load_fonts(l->context, diags);
    bool known = !language;
    for (uint32_t i = 0; i < scene->language_count && !known; ++i) known = strcmp(scene->languages[i], language) == 0;
    if (!known) {
        diag_add(diags, kErrStringTable, scene->path, scene->strings_line, 1, "language '%s' is not in languages (%s)",
                 language, scene->language_count ? "see [scene]" : "the scene has none");
    } else if (scene->language_count) {
        set_language(l, language ? language : scene->languages[0], diags);
    }
    for (uint32_t i = 0; i < scene->ui_count; ++i) {
        scene::Scene* doc = scene::load_screen(scene->ui[i], diags);
        if (!doc) continue;
        l->docs[l->count] = doc;
        l->screens[l->count++] = ui::build_screen(l->context, doc);
    }
    if (diags->count != errors_before) {
        destroy_ui_layer(l);
        *failed = true;
        return nullptr;
    }
    return l;
}

void destroy_ui_layer(UiLayer* l) {
    if (!l) return;
    for (uint32_t i = 0; i < l->count; ++i) {
        ui::destroy_screen(l->screens[i]);
        scene::free_scene(l->docs[i]);
    }
    render::destroy_ui_renderer(l->renderer);
    ui::free_draw_list(&l->list);
    scene::free_string_table(l->strings);
    ui::destroy_context(l->context);
    scene::destroy_ui_state(l->state);
    delete l;
}

bool attach_gpu(UiLayer* l, rhi::Device* device, const char* shader_dir) {
    const ui::ContextDesc sizes;
    l->device = device;
    l->renderer = render::create_ui_renderer(device, shader_dir, sizes.glyph_atlas_size, sizes.image_atlas_size);
    return l->renderer != nullptr;
}

void update_ui(UiLayer* l, const platform::Input* input, float width, float height, float dt) {
    ZARO_ZONE("ui.update");
    l->width = width;
    l->height = height;
    l->back_used = false;
    ui::UiInput in = input ? to_ui_input(*input) : ui::UiInput{};
    ui::begin_frame(l->context);
    for (uint32_t i = l->count; i-- > 0;) {
        ui::Screen* s = l->screens[i];
        ui::update_screen(s, in, width, height, ui::screen_scale(s, width, height), dt);
        // An open modal takes all input; a screen that holds the pointer or the focus hides that
        // input from the screens below.
        if (ui::has_open_modal(s)) {
            in = ui::UiInput{};
            continue;
        }
        if (ui::pointer_captured(s)) {
            in.pointer_down = in.pointer_pressed = in.pointer_released = false;
            in.pointer_x = in.pointer_y = -1.0f;
            in.wheel_y = 0.0f;
        }
        if (ui::focused_node(s) != ui::kNoNode) {
            in.nav_up = in.nav_down = in.nav_left = in.nav_right = in.nav_confirm = false;
        }
    }
}

bool ui_used_back(const UiLayer* l) { return l && l->back_used; }

void render_ui_layer(UiLayer* l) {
    if (!l->renderer) return;
    ZARO_ZONE("ui.render");
    ui::reset_draw_list(&l->list, l->width, l->height);
    for (uint32_t i = 0; i < l->count; ++i) ui::draw_screen(l->screens[i], &l->list);
    const ui::AtlasUpdate glyphs = ui::take_atlas_update(ui::context_text(l->context));
    const ui::AtlasUpdate images = ui::take_image_update(l->context);
    render::UiFrame frame;
    frame.quads = (const render::UiQuad*)l->list.quads;
    frame.quad_count = l->list.count;
    frame.atlas = glyphs.pixels;
    frame.atlas_size = glyphs.size;
    frame.atlas_x0 = glyphs.x0, frame.atlas_y0 = glyphs.y0, frame.atlas_x1 = glyphs.x1, frame.atlas_y1 = glyphs.y1;
    frame.images = images.pixels;
    frame.image_atlas_size = images.size;
    frame.images_x0 = images.x0, frame.images_y0 = images.y0, frame.images_x1 = images.x1, frame.images_y1 = images.y1;
    render::render_ui(l->renderer, frame, nullptr);
}

uint32_t screen_count(const UiLayer* l) { return l ? l->count : 0; }
ui::Screen* screen_at(UiLayer* l, uint32_t i) { return l && i < l->count ? l->screens[i] : nullptr; }

scene::Scene* const* screen_documents(UiLayer* l) { return l->docs; }
scene::UiState* ui_state(UiLayer* l) { return l ? l->state : nullptr; }

bool set_language(UiLayer* l, const char* language, DiagList* diags) {
    scene::StringTable* table = scene::load_string_table(l->scene->strings, language, diags);
    if (!table) return false;
    scene::free_string_table(l->strings);
    l->strings = table;
    ui::set_strings(l->context, table);
    if (l->state) scene::set_value_string(l->state, "language", language);
    return true;
}

bool click(UiLayer* l, const char* screen, const char* node) {
    for (uint32_t i = l->count; i-- > 0;) {
        ui::Screen* s = l->screens[i];
        if (strcmp(ui::screen_name(s), screen) == 0) return ui::click_node(s, ui::find_node(s, node));
        if (ui::has_open_modal(s)) return false;  // a modal above the target takes the click
    }
    return false;
}

const char* node_text(UiLayer* l, const char* screen, const char* node, char* out, size_t size) {
    ui::Screen* s = find_screen(l, screen);
    const int n = s ? ui::find_node(s, node) : ui::kNoNode;
    return n == ui::kNoNode ? nullptr : ui::node_text(s, n, out, size);
}

ui::Screen* find_screen(UiLayer* l, const char* name) {
    for (uint32_t i = 0; l && i < l->count; ++i) {
        if (strcmp(ui::screen_name(l->screens[i]), name) == 0) return l->screens[i];
    }
    return nullptr;
}

bool check_scene_ui(const scene::Scene* scene, DiagList* diags) {
    const uint32_t errors_before = diags->count;
    scene::StringTable* tables[scene::kMaxLanguages] = {};
    uint32_t table_count = 0;
    bool tables_ok = true;
    for (uint32_t i = 0; i < scene->language_count; ++i) {
        tables[table_count] = scene::load_string_table(scene->strings, scene->languages[i], diags);
        if (tables[table_count]) ++table_count;
        else tables_ok = false;
    }
    if (tables_ok) scene::check_tables_agree(tables, table_count, diags);
    for (uint32_t i = 0; i < scene->ui_count; ++i) {
        scene::Scene* doc = scene::load_screen(scene->ui[i], diags);
        if (!doc) continue;
        if (tables_ok) scene::check_string_keys(doc, tables, table_count, diags);
        check_screen_images(doc, diags);
        scene::free_scene(doc);
    }
    for (uint32_t i = 0; i < table_count; ++i) scene::free_string_table(tables[i]);
    return diags->count == errors_before;
}

bool check_screen_images(const scene::Scene* screen, DiagList* diags) {
    const uint32_t errors_before = diags->count;
    for (uint32_t i = 0; i < screen->entity_count; ++i) {
        const scene::Entity& e = screen->entities[i];
        const scene::ComponentData* cd = scene::find_component_data(&e, &ui::kImageDesc);
        if (!cd) continue;
        const ui::Image* img = (const ui::Image*)cd->data;
        const int line = cd->field_lines[0] ? cd->field_lines[0] : e.line;
        if (!img->path[0]) {
            diag_add(diags, kErrImageLoad, screen->path, line, 1, "node '%s': Image needs a path", e.name);
            continue;
        }
        char path[scene::kPathCapacity];
        path_join_relative(screen->path, img->path, path, sizeof(path));
        FILE* f = fopen(path, "rb");
        if (f) fclose(f);
        else diag_add(diags, kErrImageLoad, screen->path, line, 1, "node '%s': cannot open image '%s'", e.name, path);
    }
    return diags->count == errors_before;
}

}  // namespace zaro::app
