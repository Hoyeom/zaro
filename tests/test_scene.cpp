// Scene loading: prefab overrides, defaults, and one diagnostic per problem with the right id
// and line. Run from the repository root (ctest sets WORKING_DIRECTORY).
#include "core/diag.h"
#include "core/path.h"
#include "core/reflect.h"
#include "scene/scene.h"
#include "scene/strings.h"
#include "sim/crowd_components.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

namespace {

int g_failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void test_override() {
    zaro::DiagList* diags = new zaro::DiagList();
    zaro::scene::Scene* s = zaro::scene::load_scene("tests/data/scenes/override.toml", diags);
    expect(s != nullptr, "override.toml loads");
    if (s) {
        const zaro::scene::Entity* e = zaro::scene::find_entity(s, "horde");
        expect(e != nullptr, "entity 'horde' exists");
        const auto* spawner = e ? (const zaro::sim::CrowdSpawner*)zaro::scene::get_component(
                                      e, &zaro::sim::kCrowdSpawnerDesc)
                                : nullptr;
        expect(spawner != nullptr, "horde has CrowdSpawner");
        if (spawner) {
            expect(spawner->count == 123, "entity value overrides the prefab (count = 123)");
            expect(spawner->speed == 6.5f, "prefab value is kept (speed = 6.5)");
            expect(spawner->radius == zaro::sim::CrowdSpawner{}.radius, "default is kept (radius)");
        }
    } else {
        zaro::diag_print_text(diags, stderr);
    }
    zaro::scene::free_scene(s);
    delete diags;
}

void test_errors() {
    struct SceneExpected {
        zaro::ErrorId id;
        int line;
    };
    // One entry per problem in tests/data/scenes/errors.toml, in file order of discovery.
    const SceneExpected expected[] = {
        {zaro::kErrSceneStructure, 4},         // unknown key in [scene]
        {zaro::kErrSceneUnknownComponent, 8},  // CrowdSpwner
        {zaro::kErrSceneUnknownField, 14},     // cuont
        {zaro::kErrSceneType, 15},             // speed = "fast"
        {zaro::kErrSceneRange, 16},            // radius = 99
        {zaro::kErrScenePrefab, 20},           // missing.toml
        {zaro::kErrSceneDuplicate, 18},        // second 'a'
        {zaro::kErrSceneStructure, 22},        // entity without name
    };
    const uint32_t n = sizeof(expected) / sizeof(expected[0]);
    zaro::DiagList* diags = new zaro::DiagList();
    zaro::scene::Scene* s = zaro::scene::load_scene("tests/data/scenes/errors.toml", diags);
    expect(s == nullptr, "errors.toml is rejected");
    expect(diags->count == n, "errors.toml reports exactly one diagnostic per problem");
    for (uint32_t i = 0; i < n && i < diags->count; ++i) {
        char what[96];
        snprintf(what, sizeof(what), "diagnostic %u is ZE%04d on line %d", i, (int)expected[i].id, expected[i].line);
        expect(diags->items[i].id == expected[i].id && diags->items[i].line == expected[i].line, what);
    }
    expect(diags->count > 1 && strstr(diags->items[1].message, "did you mean 'CrowdSpawner'") != nullptr,
           "unknown component suggests the closest name");
    if (g_failures) zaro::diag_print_text(diags, stderr);
    zaro::scene::free_scene(s);
    delete diags;
}

struct Expected {
    zaro::ErrorId id;
    int line;
    const char* needle;  // must appear in the message, or nullptr
};

void expect_diags(const zaro::DiagList* diags, const Expected* expected, uint32_t n, const char* file) {
    char what[256];
    snprintf(what, sizeof(what), "%s reports exactly one diagnostic per problem (%u, got %u)", file, n, diags->count);
    expect(diags->count == n, what);
    for (uint32_t i = 0; i < n && i < diags->count; ++i) {
        const zaro::Diag& d = diags->items[i];
        snprintf(what, sizeof(what), "%s diagnostic %u is ZE%04d on line %d%s%s", file, i, (int)expected[i].id,
                 expected[i].line, expected[i].needle ? " with " : "", expected[i].needle ? expected[i].needle : "");
        expect(d.id == expected[i].id && d.line == expected[i].line &&
                   (!expected[i].needle || strstr(d.message, expected[i].needle)),
               what);
    }
    if (g_failures) zaro::diag_print_text(diags, stderr);
}

void test_screen_errors() {
    // Header and node problems in file order, then the parents (resolved after every node).
    const Expected expected[] = {
        {zaro::kErrSceneStructure, 4, "refrence"},
        {zaro::kErrSceneUnknownComponent, 8, "did you mean 'Label'"},
        {zaro::kErrSceneUnknownField, 15, "did you mean 'color'"},
        {zaro::kErrSceneRange, 17, "#RRGGBB"},
        {zaro::kErrSceneRange, 23, "did you mean 'row'"},
        {zaro::kErrSceneUnknownComponent, 24, "scene component"},
        {zaro::kErrSceneParent, 13, "did you mean 'bar'"},
        {zaro::kErrSceneParent, 21, "must come before"},
    };
    zaro::DiagList* diags = new zaro::DiagList();
    zaro::scene::Scene* s = zaro::scene::load_screen("tests/data/screens/errors.toml", diags);
    expect(s == nullptr, "screens/errors.toml is rejected");
    expect_diags(diags, expected, sizeof(expected) / sizeof(expected[0]), "screens/errors.toml");
    delete diags;

    diags = new zaro::DiagList();
    s = zaro::scene::load_scene("tests/data/screens/widget_in_scene.toml", diags);
    const Expected widget[] = {{zaro::kErrSceneUnknownComponent, 6, "UI widget"}};
    expect_diags(diags, widget, 1, "widget_in_scene.toml");
    delete diags;

    diags = new zaro::DiagList();
    s = zaro::scene::load_scene("tests/data/screens/keys.toml", diags);
    const Expected wrong_kind[] = {{zaro::kErrSceneStructure, 1, "got a screen file"}};
    expect_diags(diags, wrong_kind, 1, "load_scene(screen file)");
    delete diags;
}

void test_screen_tree() {
    zaro::DiagList* diags = new zaro::DiagList();
    zaro::scene::Scene* s = zaro::scene::load_document("examples/hello/ui/settings.toml", diags);
    expect(s && s->kind == zaro::scene::DocKind::kScreen && s->navigable, "settings.toml loads as a navigable screen");
    if (s) {
        const zaro::scene::Entity* title = zaro::scene::find_entity(s, "title");
        expect(title && title->parent_index >= 0 && strcmp(s->entities[title->parent_index].name, "panel") == 0,
               "a node links to its parent");
        expect(s->entities[0].parent_index == -1, "a node without parent is top level");
        const zaro::scene::ComponentData* label = title ? zaro::scene::find_component_data(title, &zaro::ui::kLabelDesc)
                                                        : nullptr;
        expect(label && label->field_lines[0] > title->line, "field lines are recorded");
        expect(s->transition == zaro::scene::kTransitionPop && s->dismissible && s->skippable &&
                   s->transition_time > 0.14f && s->transition_time < 0.16f,
               "the popup pops in, is dismissible and skippable, with the default 0.15 s");
    }
    zaro::scene::free_scene(s);
    delete diags;
}

void test_strings() {
    zaro::DiagList* diags = new zaro::DiagList();
    zaro::scene::StringTable* ko = zaro::scene::load_string_table("tests/data/screens/strings/", "ko", diags);
    zaro::scene::StringTable* en = zaro::scene::load_string_table("tests/data/screens/strings/", "en", diags);
    expect(ko && en && diags->count == 0, "string tables load");
    if (!ko || !en) return;
    expect(strcmp(zaro::scene::find_string(ko, "a.title"), "\xec\xa0\x9c\xeb\xaa\xa9") == 0, "nested tables join keys with '.'");
    expect(zaro::scene::find_string(ko, "a.missing") == nullptr, "missing keys are nullptr");
    const zaro::scene::StringTable* tables[2] = {ko, en};
    zaro::scene::check_tables_agree(tables, 2, diags);
    const Expected agree[] = {
        {zaro::kErrStringTable, 0, "missing 'a.body'"},
        {zaro::kErrStringTable, 3, "'a.extra' is not in"},
    };
    expect_diags(diags, agree, 2, "check_tables_agree");
    delete diags;

    diags = new zaro::DiagList();
    zaro::scene::Scene* screen = zaro::scene::load_screen("tests/data/screens/keys.toml", diags);
    expect(screen != nullptr, "keys.toml loads");
    if (screen) zaro::scene::check_string_keys(screen, tables, 2, diags);
    const Expected keys[] = {
        {zaro::kErrStringKey, 7, "did you mean '@a.title'"},
        {zaro::kErrStringKey, 7, "did you mean '@a.title'"},
        {zaro::kErrStringKey, 12, "en.toml"},
    };
    expect_diags(diags, keys, 3, "check_string_keys");
    zaro::scene::free_scene(screen);
    zaro::scene::free_string_table(ko);
    zaro::scene::free_string_table(en);
    delete diags;
}

void test_themes() {
    // Tokens resolve in the screen and in its prefabs (hello's settings popup, from its design kit).
    zaro::DiagList* diags = new zaro::DiagList();
    zaro::scene::Scene* s = zaro::scene::load_screen("examples/hello/ui/settings.toml", diags);
    expect(s != nullptr, "the themed settings screen loads");
    if (s) {
        const zaro::scene::Entity* panel = zaro::scene::find_entity(s, "panel");
        const auto* layout = panel ? (const zaro::ui::Layout*)zaro::scene::get_component(panel, &zaro::ui::kLayoutDesc)
                                   : nullptr;
        const auto* fill = panel ? (const zaro::ui::Panel*)zaro::scene::get_component(panel, &zaro::ui::kPanelDesc)
                                 : nullptr;
        expect(layout && layout->padding[0] == 32.0f && layout->gap == 20.0f, "prefab tokens resolve: $space-8, $space-5");
        expect(fill && fill->color == 0xfff0f5f7u && fill->radius == 20.0f, "prefab tokens resolve: $surface, $radius-lg");
        const zaro::scene::Entity* title = zaro::scene::find_entity(s, "title");
        const auto* label = title ? (const zaro::ui::Label*)zaro::scene::get_component(title, &zaro::ui::kLabelDesc)
                                  : nullptr;
        expect(label && label->size == 32.0f, "type style tokens resolve: $title.size");
    }
    zaro::scene::free_scene(s);
    delete diags;

    diags = new zaro::DiagList();
    s = zaro::scene::load_screen("tests/data/screens/theme/tokens.toml", diags);
    const Expected tokens[] = {
        {zaro::kErrSceneToken, 11, "did you mean '$surface'"},
        {zaro::kErrSceneToken, 12, "no number token 'surface'"},
    };
    expect_diags(diags, tokens, 2, "theme/tokens.toml");
    zaro::scene::free_scene(s);
    delete diags;

    diags = new zaro::DiagList();
    s = zaro::scene::load_screen("tests/data/screens/theme/no_theme.toml", diags);
    const Expected no_theme[] = {{zaro::kErrSceneToken, 7, "has no theme"}};
    expect_diags(diags, no_theme, 1, "theme/no_theme.toml");
    delete diags;

    diags = new zaro::DiagList();
    s = zaro::scene::load_screen("tests/data/screens/theme/missing_theme.toml", diags);
    const Expected missing[] = {{zaro::kErrSceneStructure, 3, "cannot open theme"}};
    expect_diags(diags, missing, 1, "theme/missing_theme.toml");
    delete diags;
}

void test_paths() {
    struct Case {
        const char* base;
        const char* rel;
        const char* want;
    };
    const Case cases[] = {
        {"game/tests/a.test.toml", "../scene.toml", "game/scene.toml"},
        {"game/scene.toml", "prefabs/./p.toml", "game/prefabs/p.toml"},
        {"t.toml", "../up.toml", "../up.toml"},
        {"a/b.toml", "/abs/x.toml", "/abs/x.toml"},
        {"C:/x\\scene.toml", "s.luau", "C:/x/s.luau"},
        {"a/b/c.toml", "../../d.toml", "d.toml"},
    };
    for (const Case& c : cases) {
        char out[256];
        zaro::path_join_relative(c.base, c.rel, out, sizeof(out));
        char what[512];
        snprintf(what, sizeof(what), "path_join_relative(%s, %s) = %s, want %s", c.base, c.rel, out, c.want);
        expect(strcmp(out, c.want) == 0, what);
    }
}

}  // namespace

int main() {
    test_paths();
    zaro::sim::register_crowd_components();
    zaro::ui::register_ui_components();
    test_override();
    test_errors();
    test_screen_errors();
    test_screen_tree();
    test_strings();
    test_themes();
    if (g_failures) {
        fprintf(stderr, "test_scene: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_scene: ok\n");
    return 0;
}
