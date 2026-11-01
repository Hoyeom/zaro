// Non-interactive tools behind `zaro schema` and `zaro check`.
#pragma once

namespace zaro::app {

// Registers every component the engine knows. Call once before loading scenes.
void register_all_components();

// `zaro schema`: prints the component schema as JSON. Returns the exit code.
int print_schema();

// `zaro check <scene>`: loads and validates a scene and compiles its script (running only the
// script's top level, no hooks). Returns 0 when valid,
// 1 when the scene has errors (printed as text, or as a JSON array with json = true).
int check_scene(const char* path, bool json);

}  // namespace zaro::app
