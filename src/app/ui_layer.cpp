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
#include "ui/navigator.h"
#include "ui/screen.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

namespace zaro::app {

static_assert(sizeof(ui::Quad) == sizeof(render::UiQuad), "ui::Quad and render::UiQuad must match");

namespace {

constexpr uint32_t kMaxClicks = 64;

}  // namespace

struct PendingClick {
    char action[reflect::kStringCapacity];
    char screen[scene::kNameCapacity];
    char node[scene::kNameCapacity];
    const ui::Screen* source;  // for "close": the screen the button is on
};

struct UiLayer {
    scene::Scene* scene = nullptr;
    ui::Context* context = nullptr;
    ui::Navigator* nav = nullptr;
    scene::StringTable* strings = nullptr;
    scene::UiState* state = nullptr;
    script::UiHost host{};
    script::Script* script = nullptr;
    ui::DrawList list;
    rhi::Device* device = nullptr;
    render::UiRenderer* renderer = nullptr;
    float width = 0.0f, height = 0.0f;
    bool at_root = false;
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
    return u;
}

// The first diagnostic's message into err, then the list is emptied.
bool take_error(DiagList* d, char* err, uint32_t size) {
    if (!d->count) return true;
    snprintf(err, size, "%s", d->items[0].message);
    d->count = d->dropped = 0;
    return false;
}

void do_back(UiLayer* l, bool ask_claim, DiagList* diags, bool* ok) {
    bool claimed = false;
    if (ask_claim && l->script) {
        const int owner = ui::focus_owner(l->nav);
        const scene::Scene* doc = owner >= 0 ? ui::entry_document(l->nav, (uint32_t)owner) : nullptr;
        if (doc && !script::call_back(l->script, doc->name, &claimed, diags)) *ok = false;
    }
    if (ui::back(l->nav, claimed) == ui::BackResult::kBackAtRoot) l->at_root = true;
}

// --- script::UiHost, user = UiLayer*

scene::Scene* host_find_screen(void* user, const char* name) { return find_document((UiLayer*)user, name); }

uint32_t host_screen_names(void* user, const char** names, uint32_t max) {
    const UiLayer* l = (const UiLayer*)user;
    uint32_t n = 0;
    for (uint32_t i = 0; i < ui::entry_count(l->nav) && n < max; ++i) names[n++] = ui::entry_document(l->nav, i)->name;
    return n + ui::catalog_names(l->nav, names + n, max - n);
}

bool host_language(void* user, const char* code, char* err, uint32_t err_size) {
    return set_language((UiLayer*)user, code, err, err_size);
}

bool host_open(void* user, const char* name, const char* key, bool wait, bool force, bool unskippable, char* err,
               uint32_t err_size) {
    UiLayer* l = (UiLayer*)user;
    ui::OpenOptions o;
    o.key = key;
    o.wait = wait;
    o.force = force;
    o.unskippable = unskippable;
    DiagList* d = new DiagList();
    ui::open(l->nav, name, o, d);
    const bool ok = take_error(d, err, err_size);
    delete d;
    return ok;
}

bool host_ready(void* user, const char* name) { return ui::ready(((UiLayer*)user)->nav, name); }
bool host_close(void* user, const char* name) { return ui::close(((UiLayer*)user)->nav, name); }

void host_back(void* user) {
    UiLayer* l = (UiLayer*)user;
    if (ui::back(l->nav, false) == ui::BackResult::kBackAtRoot) l->at_root = true;
}

void host_home(void* user) { ui::home(((UiLayer*)user)->nav); }
void host_abort_all(void* user) { ui::abort_all(((UiLayer*)user)->nav); }
const char* host_state(void* user, const char* name) { return screen_state((UiLayer*)user, name); }

}  // namespace

UiLayer* create_ui_layer(scene::Scene* scene, const char* language, DiagList* diags, bool* failed) {
    *failed = false;
    if (!scene || (!scene->ui_count && !scene->screen_count)) return nullptr;
    const uint32_t errors_before = diags->count;
    UiLayer* l = new UiLayer();
    l->scene = scene;
    l->state = scene::create_ui_state();
    l->context = ui::create_context();
    l->nav = ui::create_navigator(l->context);
    ui::set_ui_state(l->context, l->state);
    load_fonts(l->context, diags);
    char err[300];
    const char* lang = language ? language : (scene->language_count ? scene->languages[0] : nullptr);
    if (lang && !set_language(l, lang, err, sizeof(err))) {
        diag_add(diags, kErrStringTable, scene->path, scene->strings_line, 1, "%s", err);
    }
    for (uint32_t i = 0; i < scene->ui_count; ++i) ui::add_root(l->nav, scene->ui[i], diags);
    for (uint32_t i = 0; i < scene->screen_count; ++i) ui::add_catalog(l->nav, scene->screens[i], diags);
    l->host = script::UiHost{l,          host_find_screen, host_screen_names, host_language, host_open,
                             host_ready, host_close,       host_back,         host_home,     host_abort_all,
                             host_state};
    if (diags->count != errors_before) {
        destroy_ui_layer(l);
        *failed = true;
        return nullptr;
    }
    return l;
}

void destroy_ui_layer(UiLayer* l) {
    if (!l) return;
    ui::destroy_navigator(l->nav);
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

const script::UiHost* ui_host(UiLayer* l) { return l ? &l->host : nullptr; }
scene::UiState* ui_state(UiLayer* l) { return l ? l->state : nullptr; }
void set_script(UiLayer* l, script::Script* s) { l->script = s; }

void update_ui(UiLayer* l, const platform::Input* input, float width, float height, float dt) {
    ZARO_ZONE("ui.update");
    l->width = width;
    l->height = height;
    l->at_root = false;
    ui::begin_frame(l->context);
    ui::update(l->nav, input ? to_ui_input(*input) : ui::UiInput{}, width, height, dt);
}

bool handle_ui(UiLayer* l, bool back_pressed, DiagList* diags) {
    // Copy the clicks first: hooks and navigation change the stack while they run.
    PendingClick* clicks = new PendingClick[kMaxClicks];
    uint32_t count = 0;
    for (uint32_t i = 0; i < ui::entry_count(l->nav); ++i) {
        const ui::Screen* s = ui::entry_screen(l->nav, i);
        for (uint32_t k = 0; k < ui::event_count(s) && count < kMaxClicks; ++k) {
            const ui::Event* e = ui::event_at(s, k);
            if (e->kind != ui::EventKind::kClick || !e->action[0]) continue;
            PendingClick& c = clicks[count++];
            snprintf(c.action, sizeof(c.action), "%s", e->action);
            snprintf(c.screen, sizeof(c.screen), "%s", ui::screen_name(s));
            snprintf(c.node, sizeof(c.node), "%s", ui::node_name(s, e->node));
            c.source = s;
        }
    }
    bool ok = true;
    for (uint32_t i = 0; i < count; ++i) {
        const PendingClick& c = clicks[i];
        if (strncmp(c.action, "open:", 5) == 0) ui::open(l->nav, c.action + 5, ui::OpenOptions{}, diags);
        else if (strcmp(c.action, "close") == 0) ui::close_screen(l->nav, c.source);
        else if (strcmp(c.action, "back") == 0) do_back(l, true, diags, &ok);
        else if (strcmp(c.action, "home") == 0) ui::home(l->nav);
        if (l->script && !script::call_ui(l->script, c.action, c.screen, c.node, diags)) ok = false;
    }
    delete[] clicks;
    if (back_pressed) do_back(l, true, diags, &ok);
    static const char* const kEvents[] = {"created", "opened", "closed", "cancelled", "back_at_root"};
    // Hooks may open or close screens, which adds events; those are sent in the same pass.
    for (uint32_t i = 0; i < ui::event_count(l->nav); ++i) {
        const ui::NavEvent e = *ui::event_at(l->nav, i);
        if (l->script && !script::call_screen(l->script, kEvents[(int)e.kind], e.screen, diags)) ok = false;
    }
    ui::clear_events(l->nav);
    return ok;
}

bool back_at_root(const UiLayer* l) { return l && l->at_root; }
bool ui_captured_input(const UiLayer* l) { return l && ui::input_captured(l->nav); }

void render_ui_layer(UiLayer* l) {
    if (!l->renderer) return;
    ZARO_ZONE("ui.render");
    ui::reset_draw_list(&l->list, l->width, l->height);
    ui::draw(l->nav, &l->list);
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

bool set_language(UiLayer* l, const char* language, char* err, size_t err_size) {
    const scene::Scene* sc = l->scene;
    bool known = false;
    const char* names[scene::kMaxLanguages];
    for (uint32_t i = 0; i < sc->language_count; ++i) {
        known = known || strcmp(sc->languages[i], language) == 0;
        names[i] = sc->languages[i];
    }
    if (!known) {
        const char* hint = reflect::suggest_name(language, names, sc->language_count);
        snprintf(err, err_size, "language '%s' is not in the scene's languages%s%s%s", language,
                 hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
        return false;
    }
    DiagList* d = new DiagList();
    scene::StringTable* table = scene::load_string_table(sc->strings, language, d);
    const bool ok = take_error(d, err, (uint32_t)err_size);
    delete d;
    if (!table) return false;
    scene::free_string_table(l->strings);
    l->strings = table;
    ui::set_strings(l->context, table);
    scene::set_value_string(l->state, "language", language);
    return ok;
}

bool click(UiLayer* l, const char* screen, const char* node) {
    for (uint32_t i = ui::entry_count(l->nav); i-- > 0;) {
        ui::Screen* s = ui::entry_screen(l->nav, i);
        const ui::EntryState st = ui::entry_state(l->nav, i);
        if (strcmp(ui::screen_name(s), screen) == 0 && st != ui::EntryState::kClosing) {
            return st == ui::EntryState::kOpen && ui::click_node(s, ui::find_node(s, node));
        }
        if (st != ui::EntryState::kOpening && ui::has_open_modal(s)) return false;  // a modal above takes it
    }
    return false;
}

scene::Scene* find_document(UiLayer* l, const char* screen) {
    const int i = ui::find_entry(l->nav, screen);
    return i < 0 ? nullptr : ui::entry_document(l->nav, (uint32_t)i);
}

const char* node_text(UiLayer* l, const char* screen, const char* node, char* out, size_t size) {
    const int i = ui::find_entry(l->nav, screen);
    ui::Screen* s = i < 0 ? nullptr : ui::entry_screen(l->nav, (uint32_t)i);
    const int n = s ? ui::find_node(s, node) : ui::kNoNode;
    return n == ui::kNoNode ? nullptr : ui::node_text(s, n, out, size);
}

const char* screen_state(UiLayer* l, const char* screen) {
    const int i = ui::find_entry(l->nav, screen);
    return i < 0 ? "closed" : ui::entry_state_name(ui::entry_state(l->nav, (uint32_t)i));
}

namespace {

// open:<screen> actions must name a screen of the scene.
void check_open_actions(const scene::Scene* screen, const char* const* names, uint32_t count, DiagList* diags) {
    for (uint32_t i = 0; i < screen->entity_count; ++i) {
        const scene::Entity& e = screen->entities[i];
        const scene::ComponentData* cd = scene::find_component_data(&e, &ui::kButtonDesc);
        const ui::Button* b = cd ? (const ui::Button*)cd->data : nullptr;
        if (!b || strncmp(b->action, "open:", 5) != 0) continue;
        bool found = false;
        for (uint32_t k = 0; k < count; ++k) found = found || strcmp(names[k], b->action + 5) == 0;
        if (found) continue;
        const char* hint = reflect::suggest_name(b->action + 5, names, count);
        const int line = cd->field_lines[1] ? cd->field_lines[1] : e.line;  // Button.action
        diag_add(diags, kErrSceneStructure, screen->path, line, 1,
                 "node '%s': '%s' opens no screen of the scene's screens%s%s%s", e.name, b->action,
                 hint ? " (did you mean 'open:" : "", hint ? hint : "", hint ? "'?)" : "");
    }
}

}  // namespace

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
    // Openable screens first, so their names are known when the actions are checked.
    const uint32_t total = scene->ui_count + scene->screen_count;
    scene::Scene* docs[scene::kMaxSceneScreens * 5] = {};
    const char* names[scene::kMaxSceneScreens * 4];
    uint32_t name_count = 0;
    for (uint32_t i = 0; i < total; ++i) {
        const bool root = i < scene->ui_count;
        docs[i] = scene::load_screen(root ? scene->ui[i] : scene->screens[i - scene->ui_count], diags);
        if (docs[i] && !root) names[name_count++] = docs[i]->name;
    }
    for (uint32_t i = 0; i < total; ++i) {
        if (!docs[i]) continue;
        if (tables_ok) scene::check_string_keys(docs[i], tables, table_count, diags);
        check_screen_images(docs[i], diags);
        check_open_actions(docs[i], names, name_count, diags);
    }
    for (uint32_t i = 0; i < total; ++i) scene::free_scene(docs[i]);
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
