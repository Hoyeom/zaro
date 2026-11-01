#include "app/tools.h"

#include "core/diag.h"
#include "core/reflect.h"
#include "platform/platform.h"
#include "scene/scene.h"
#include "script/script.h"
#include "sim/crowd_components.h"

#include <stdio.h>

namespace zaro::app {

bool prepare_process() { return platform::prepare_process(); }

void register_all_components() { sim::register_crowd_components(); }

int print_schema() {
    register_all_components();
    reflect::write_schema_json(stdout);
    return 0;
}

int check_scene(const char* path, bool json) {
    register_all_components();
    DiagList* diags = new DiagList();
    scene::Scene* s = scene::load_scene(path, diags);
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
        printf("%s: ok (scene '%s', %u entities, %u components)\n", path, s->name, s->entity_count, components);
    } else {
        diag_print_text(diags, stderr);
    }
    const int code = s ? 0 : 1;
    scene::free_scene(s);
    delete diags;
    return code;
}

}  // namespace zaro::app
