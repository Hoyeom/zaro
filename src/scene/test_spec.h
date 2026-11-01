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
//   [[click]]                      # clicks a button of the scene's UI before that tick's on_tick
//   tick = 60
//   screen = "hud"
//   node = "settings_button"
//
//   [[expect]]                     # a widget field of a screen node (screen + node, not entity)
//   screen = "settings"
//   node = "popup"
//   component = "Layout"
//   field = "visible"
//   equals = true
//
//   [[expect]]                     # or the text a Label or Button shows, strings and values applied
//   screen = "hud"
//   node = "speed"
//   text = "속도 7.5"
//
//   [[back]]                       # a back press (Escape, Android back) before that tick's on_tick
//   tick = 90
//
//   [[expect]]                     # or where a screen is in its lifecycle (D-021)
//   screen = "settings"
//   state = "closed"               # opening, entering, open, closing or closed
//
// A script that raises an error (for example a failed assert) also fails the test, and so does a
// click on a button that is hidden, disabled or behind a modal.
#pragma once

#include "core/diag.h"
#include "core/reflect.h"

#include <stdint.h>

namespace zaro::scene {

struct Expect {
    char screen[64];  // empty: `entity` names a scene entity; otherwise it names a node of this screen
    char entity[64];
    const reflect::ComponentDesc* component;
    const reflect::FieldDesc* field;
    bool has_equals, has_min, has_max;
    double equals_number;  // numbers and booleans (0 or 1)
    char equals_string[reflect::kStringCapacity];
    double min, max;
    bool has_text;
    char text[256];
    bool has_state;
    char state[16];
    int line;
};

// A [[click]], or a [[back]] press (back = true, no screen or node).
struct Click {
    bool back;
    uint32_t tick;
    char screen[64];
    char node[64];
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
    static constexpr uint32_t kMaxClicks = 64;
    Click clicks[kMaxClicks];
    uint32_t click_count;
};

// Parses a test file. Problems are ZE0600 diagnostics with file and line; returns false on any.
bool load_test_spec(const char* path, TestSpec* spec, DiagList* diags);

}  // namespace zaro::scene
