// Non-interactive tools behind `zaro schema` and `zaro check`.
#pragma once

namespace zaro::app {

// Process setup before any file is read (Android: unpack the APK's assets and log to logcat; see
// platform::prepare_process). Returns false if the process cannot run.
bool prepare_process();

// Registers every component the engine knows. Call once before loading scenes.
void register_all_components();

// `zaro schema`: prints the component schema as JSON. Returns the exit code.
int print_schema();

// `zaro check <file>`: loads and validates a scene (with its screens, string tables, sprite sheets
// and script, running only the script's top level) or a single screen file. Returns 0 when valid,
// 1 when the scene has errors (printed as text, or as a JSON array with json = true).
int check_scene(const char* path, bool json);

// `zaro test [paths...] [--update] [--json]`: runs every *.test.toml found in the paths (files or
// directories, default "tests") without a GPU. --update writes each run's state hash as the new
// golden. Returns 0 when all tests pass, 1 on a failure or when no test was found, 2 on bad paths.
int run_scene_tests(const char* const* paths, int count, bool update, bool json);

// `zaro cook <scene.toml | sheet.toml> [--force]`: cooks the PNG of every SVG sprite sheet the
// scene's sprites name (or of one sheet file) whose content hash changed (D-025); --force cooks
// them all again. Runs also cook on demand, so this is for exports and CI. Returns 0 on success.
int cook_art(const char* path, bool force);

struct ExportOptions {
    const char* scene_path = nullptr;
    const char* out_dir = nullptr;      // default build/android-project
    const char* runtime_dir = nullptr;  // default build/android/android-runtime next to this binary
    const char* package = nullptr;      // default dev.zaro.<scene name>
    bool portrait = false;              // default landscape
};

// `zaro export android <scene>`: writes a Gradle project (D-022) from the Android runtime package
// (cmake/ZaroAndroid.cmake) plus the scene's directory (scene tests excluded) and the files the
// scene names outside it (script, prefabs, screens, their images, sprite sheets with their images,
// string tables) under assets/game/. The APK runs `zaro run game/<scene>` unless launched with other
// arguments. Returns 0 on success,
// 1 when the scene is invalid or a file cannot be written, 2 on bad options.
int export_android(const ExportOptions& options);

// `zaro import-design <kit dir> <out dir> [--check]`: writes <out dir>/theme.toml from a Claude
// Design kit's tokens.json (project/tokens.json or tokens.json in the kit directory), and a UI
// prefab stub per kit component (components/<Comp>/README.md) that does not exist yet (D-023).
// --check only compares the theme it would write with the existing one. Returns 0 on success.
int import_design(const char* kit_dir, const char* out_dir, bool check);

}  // namespace zaro::app
