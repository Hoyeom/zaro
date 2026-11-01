// Non-interactive tools behind `zaro schema` and `zaro check`.
#pragma once

namespace zaro::app {

// Registers every component the engine knows. Call once before loading scenes.
void register_all_components();

// `zaro schema`: prints the component schema as JSON. Returns the exit code.
int print_schema();

// `zaro check <file>`: loads and validates a scene (with its screens, string tables and script,
// running only the script's top level) or a single screen file. Returns 0 when valid,
// 1 when the scene has errors (printed as text, or as a JSON array with json = true).
int check_scene(const char* path, bool json);

// `zaro test [paths...] [--update] [--json]`: runs every *.test.toml found in the paths (files or
// directories, default "tests") without a GPU. --update writes each run's state hash as the new
// golden. Returns 0 when all tests pass, 1 on a failure or when no test was found, 2 on bad paths.
int run_scene_tests(const char* const* paths, int count, bool update, bool json);

}  // namespace zaro::app
