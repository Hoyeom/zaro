// Reading [[step]] arrays (scene module only): game build files and build extensions declare steps
// the same way (scene/build_spec.h).
#pragma once

#include "core/diag.h"
#include "scene/build_spec.h"

#include <tomlc17.h>

namespace zaro::scene {

// Appends the steps of a [[step]] array to out (at most kMaxBuildSteps), adding a diagnostic with
// file and line for every problem.
void read_build_steps(const toml_datum_t& steps, const char* file, BuildStep* out, uint32_t* count,
                      DiagList* diags);

}  // namespace zaro::scene
