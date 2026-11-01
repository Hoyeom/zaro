---
name: zaro-code
description: Writing engine C++ and shaders — module layering and adding a module, third-party header ownership, the restricted C++20 subset and its replacements for banned headers, error reporting, deterministic parallel simulation, Slang shaders and their hot reload, and the Windows header-dependency repair. Load before adding a file, module or include, before adding or moving code between modules, before writing a parallel_for body or randomness in the simulation, before editing a shader or FrameConstants, and when a header edit did not rebuild a file.
---

# Engine code

## Modules

Each directory under `src/` is one module and one static library. The table in CLAUDE.md lists what
each may include; lint rule Z007 (`MODULE_DEPS` and `THIRD_PARTY_OWNERS` in tools/lint.py) enforces
it, and link dependencies in CMakeLists.txt follow the same graph.

- To add a feature, put the state and rules in `sim`, the drawing in `render`, and the connection in
  `app`. `render` draws plain data and never sees `sim` types.
- Third-party headers belong to one module only: `SDL3/` → platform, `webgpu/` → rhi,
  `TaskScheduler.h` → `core/jobs.cpp`, `tracy/` → `core/profile.h`, `tomlc17.h` → scene,
  `lua.h`/`lualib.h`/`luacode.h` → script, `stb_truetype.h` → `ui/font.cpp`. A new dependency gets an owner in `THIRD_PARTY_OWNERS`.
- A new module needs, in the same PR:
  - an entry in `MODULE_DEPS` (tools/lint.py)
  - its scope in `SCOPES` (tools/git_rules.py)
  - a row in the CLAUDE.md "Modules" table
  - a static library in CMakeLists.txt

## C++ subset (D-001)

The engine is C++20, but only a restricted subset of it, chosen for compile speed.

- Use instead of a banned header (full list: `BANNED_HEADERS` in tools/lint.py):
  - `<iostream>` / `<format>` → `<stdio.h>` (`printf`, `snprintf`)
  - `<functional>` → a function pointer plus `void* user` (see `jobs::RangeFn`)
  - `<variant>` / `<any>` → a tagged union
  - `<filesystem>` → the platform layer (`platform::file_mtime`, `platform::base_path`)
- Report errors by return value, never by exception. Log them with `log_error(ErrorId, ...)`
  (`src/core/log.h`), which prints `zaro: error[ZEnnnn]: ...`. Ids are stable and never renumbered.
  Each area has its own hundred (`ErrorId`: 0400s scene, 0500s script, …); a new error takes the
  next free id in its area's hundred, or a new hundred for a new area.
- Templates only for small containers and math. No template metaprogramming in headers.
- The plugin boundary (`include/zaro/`) is C: POD structs, function-pointer tables, a `struct_size`
  field first for forward compatibility (D-011). Prefer adding to changing.
- MSVC 19.44 compiles a bare `throw` without a diagnostic even with `/EHs-c-`. It rejects only
  `try`/`catch` (C4530) and `dynamic_cast` (C4541) under `/WX`. On MSVC, lint Z003 is the only check
  that catches `throw`.

## Simulation and parallelism (D-005)

- Gameplay runs at a fixed 60 Hz (`sim::kTickSeconds`). Rendering interpolates between ticks.
- `jobs::parallel_for` bodies write only elements inside their `[begin, end)` range and read only
  data that no iteration writes. Then results are identical for any thread count. Use double
  buffers (`nvx`/`nvz` in `src/sim/crowd.cpp`) when a step reads neighbours.
- Per-entity randomness inside parallel loops comes from `hash_u32` on entity index, tick and seed,
  never from a shared RNG.
- The `crowd_determinism` test checks this, and scene tests check golden state hashes on all three
  CI platforms (`zaro-scene`).

## Shaders

Shaders are Slang (`shaders/*.slang`). `zaro_add_shaders` in `cmake/ZaroDeps.cmake` compiles them
with `slangc -target wgsl -warnings-as-errors all` into `<bin>/shaders/`. A Slang error fails the
build with the file and line. `shaders/frame.slang` must stay byte-compatible with `FrameConstants`
in `src/render/crowd_renderer.cpp`, which a `static_assert` on its size checks.

Hot reload: a windowed `zaro run` polls the `.wgsl` files twice a second. Rebuild with
`python tools/build.py` while it runs, and the pipelines are recreated (`zaro: shaders reloaded`).

## Header edits did not rebuild a file (Windows, Korean-localized MSVC)

In parallel clean builds, Ninja sometimes does not recognize MSVC's localized `/showIncludes` prefix
and records `#deps 0` for an object (`ninja -C build/<preset> -t deps`). `tools/build.py` deletes
such ZARO objects after each build and rebuilds them alone, which records the dependencies.

- Running `cmake --build` directly skips that repair.
- Third-party objects under `_deps/` are not repaired; they change only with a dependency bump,
  which reconfigures.
- English MSVC output does not need the repair.
