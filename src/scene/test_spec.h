// Scene tests (`*.test.toml`), run by `zaro test`:
//
//   [test]
//   scene = "../scene.toml"        # required, relative to this file
//   ticks = 1300                   # required, simulation ticks to run (no GPU)
//   enemies = 2000                 # optional, overrides CrowdSpawner.count
//   seed = 7                       # optional, overrides CrowdArena.seed
//   state_hash = "afd01a7ee964883d"  # optional golden; `zaro test --update` writes it
//
//   [[expect]]                     # checked after the run, any number of them
//   entity = "horde"
//   component = "CrowdSpawner"
//   field = "speed"
//   equals = 5.0                   # or min = / max = for numbers
//
// A script that raises an error (for example a failed assert) also fails the test.
#pragma once

#include "core/diag.h"
#include "core/reflect.h"

#include <stdint.h>

namespace zaro::scene {

struct Expect {
    char entity[64];
    const reflect::ComponentDesc* component;
    const reflect::FieldDesc* field;
    bool has_equals, has_min, has_max;
    double equals_number;  // numbers and booleans (0 or 1)
    char equals_string[reflect::kStringCapacity];
    double min, max;
    int line;
};

struct TestSpec {
    char path[260];
    char scene[260];  // resolved relative to the test file
    uint32_t ticks;
    bool has_enemies, has_seed;
    uint32_t enemies;
    uint64_t seed;
    bool has_state_hash;
    uint64_t state_hash;
    int state_hash_line;
    static constexpr uint32_t kMaxExpects = 64;
    Expect expects[kMaxExpects];
    uint32_t expect_count;
};

// Parses a test file. Problems are ZE0600 diagnostics with file and line; returns false on any.
bool load_test_spec(const char* path, TestSpec* spec, DiagList* diags);

}  // namespace zaro::scene
