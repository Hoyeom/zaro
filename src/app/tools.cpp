#include "app/tools.h"

#include "app/shape_layer.h"
#include "app/sprite_layer.h"
#include "app/ui_layer.h"
#include "core/diag.h"
#include "core/reflect.h"
#include "platform/platform.h"
#include "scene/scene.h"
#include "scene/sprite_sheet.h"
#include "script/script.h"
#include "sim/crowd_components.h"
#include "sim/fx_components.h"
#include "sim/shape_components.h"
#include "sim/sprite_components.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

namespace zaro::app {

bool prepare_process() { return platform::prepare_process(); }

void register_all_components() {
    sim::register_crowd_components();
    sim::register_sprite_components();
    sim::register_fx_components();
    sim::register_shape_components();
    ui::register_ui_components();
}

int print_schema() {
    register_all_components();
    reflect::write_schema_json(stdout);
    return 0;
}

int check_scene(const char* path, bool json) {
    register_all_components();
    DiagList* diags = new DiagList();
    scene::Scene* s = scene::load_document(path, diags);
    const bool screen = s && s->kind == scene::DocKind::kScreen;
    // A scene's screens and string tables, or a screen's images.
    if (s && !(screen ? check_screen_images(s, diags) : check_scene_ui(s, diags))) {
        scene::free_scene(s);
        s = nullptr;
    }
    // Its sprite sheets, their frames and the animations the sprites name.
    if (s && !screen) {
        bool failed = false;
        SpriteLayer* sprites = create_sprite_layer(s, diags, &failed);
        failed = !check_sprite_art(sprites, diags) || failed;
        destroy_sprite_layer(sprites);
        // Its shapes: a crowd look given twice, a screen shape following the player.
        bool shapes_failed = false;
        destroy_shape_layer(create_shape_layer(s, diags, &shapes_failed));
        failed = shapes_failed || failed;
        if (failed) {
            scene::free_scene(s);
            s = nullptr;
        }
    }
    // Compile the scene's script and run its top level (hooks are not called).
    if (s && s->script[0]) {
        script::ScriptDesc sd;
        sd.path = s->script;
        sd.scene = s;
        script::Script* sc = script::load_script(sd, diags);
        if (!sc) {
            scene::free_scene(s);
            s = nullptr;
        }
        script::destroy_script(sc);
    }
    if (json) {
        diag_print_json(diags, stdout);
    } else if (s) {
        uint32_t components = 0;
        for (uint32_t i = 0; i < s->entity_count; ++i) components += s->entities[i].component_count;
        if (screen) printf("%s: ok (screen '%s', %u nodes, %u widgets)\n", path, s->name, s->entity_count, components);
        else if (s->ui_count || s->screen_count) {
            printf("%s: ok (scene '%s', %u entities, %u components, %u screens, %u languages)\n", path, s->name,
                   s->entity_count, components, s->ui_count + s->screen_count, s->language_count);
        } else {
            printf("%s: ok (scene '%s', %u entities, %u components)\n", path, s->name, s->entity_count, components);
        }
    } else {
        diag_print_text(diags, stderr);
    }
    const int code = s ? 0 : 1;
    scene::free_scene(s);
    delete diags;
    return code;
}

namespace {

struct CookRun {
    bool force;
    DiagList* diags;
    bool failed;
    uint32_t cooked, current;
};

void cook_one(CookRun& run, const scene::SpriteSheet* sheet) {
    if (!sheet->svg_count) return;
    bool drew = false;
    if (!ensure_sheet_art(sheet, run.force, run.diags, &drew)) {
        run.failed = true;
    } else if (drew) {
        printf("cooked %s (%u frames)\n", sheet->image, sheet->frame_count);
        ++run.cooked;
    } else {
        ++run.current;
    }
}

// A sheet file has a line starting with [sheet]; anything else is read as a scene.
bool is_sheet_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char line[256];
    bool sheet = false;
    while (!sheet && fgets(line, sizeof(line), f)) sheet = strncmp(line, "[sheet]", 7) == 0;
    fclose(f);
    return sheet;
}

}  // namespace

int cook_art(const char* path, bool force) {
    register_all_components();
    CookRun run{force, new DiagList(), false, 0, 0};
    scene::Scene* s = nullptr;
    scene::SpriteSheet* single = nullptr;
    SpriteLayer* layer = nullptr;
    if (is_sheet_file(path)) {
        single = scene::load_sprite_sheet(path, run.diags);
        run.failed = !single;
        if (single) cook_one(run, single);
    } else {
        s = scene::load_scene(path, run.diags);
        run.failed = !s;
        if (s) layer = create_sprite_layer(s, run.diags, &run.failed);
        for (uint32_t i = 0; i < sprite_sheet_count(layer); ++i) cook_one(run, sprite_sheet_at(layer, i));
    }
    if (run.failed) diag_print_text(run.diags, stderr);
    else printf("%s: %u sheet(s) cooked, %u up to date\n", path, run.cooked, run.current);
    destroy_sprite_layer(layer);
    scene::free_sprite_sheet(single);
    scene::free_scene(s);
    delete run.diags;
    return run.failed ? 1 : 0;
}

}  // namespace zaro::app
