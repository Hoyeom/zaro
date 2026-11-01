// Path helpers shared by the scene, prefab, script and test-file loaders.
#pragma once

#include <stddef.h>

namespace zaro {

// Resolves `rel` against the directory of `base_file` and collapses "." and ".." segments, so
// "game/tests/../scene.toml" becomes "game/scene.toml". An absolute `rel` (leading '/' or a
// drive letter) is used as is. Separators in the result are '/'.
void path_join_relative(const char* base_file, const char* rel, char* out, size_t size);

}  // namespace zaro
