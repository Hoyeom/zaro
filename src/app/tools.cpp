#include "app/tools.h"

#include "app/ui_layer.h"
#include "core/diag.h"
#include "core/reflect.h"
#include "platform/platform.h"
#include "scene/scene.h"
#include "script/script.h"
#include "sim/crowd_components.h"
#include "ui/widgets.h"

#include <stdio.h>

namespace zaro::app {

bool prepare_process() { return platform::prepare_process(); }

void register_all_components() {
    sim::register_crowd_components();
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

}  // namespace zaro::app
