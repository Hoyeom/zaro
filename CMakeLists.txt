cmake_minimum_required(VERSION 3.28)
project(zaro VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
# No C++20 modules: skipping the dependency scan keeps builds fast.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# CMake's MSVC defaults add /EHsc; ZARO builds without exceptions (D-001).
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_C_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(ZaroCompilerFlags)

option(ZARO_TRACY "Build with the Tracy profiler client" OFF)
option(ZARO_BUILD_TESTS "Build tests" ON)

include(ZaroDeps)

# Modules, one static library each. Link dependencies follow the layering in CLAUDE.md
# ("Modules"); tools/lint.py rule Z007 enforces the same graph on #include lines.

add_library(zaro_core STATIC
  src/core/version.cpp src/core/log.cpp src/core/jobs.cpp src/core/png.cpp
  src/core/reflect.cpp src/core/diag.cpp)
target_include_directories(zaro_core PUBLIC include src)
target_link_libraries(zaro_core PRIVATE enkiTS)
if(ZARO_TRACY)
  target_link_libraries(zaro_core PUBLIC Tracy::TracyClient)
  target_compile_definitions(zaro_core PUBLIC ZARO_TRACY)
endif()
zaro_target_defaults(zaro_core)

add_library(zaro_platform STATIC src/platform/platform_sdl.cpp)
target_link_libraries(zaro_platform PUBLIC zaro_core PRIVATE SDL3::SDL3-static)
zaro_target_defaults(zaro_platform)

add_library(zaro_rhi STATIC src/rhi/rhi_wgpu.cpp)
target_link_libraries(zaro_rhi PUBLIC zaro_platform PRIVATE zaro_wgpu)
zaro_target_defaults(zaro_rhi)

add_library(zaro_sim STATIC src/sim/crowd.cpp src/sim/crowd_components.cpp)
target_link_libraries(zaro_sim PUBLIC zaro_core)
zaro_target_defaults(zaro_sim)

add_library(zaro_scene STATIC src/scene/scene.cpp src/scene/test_spec.cpp)
target_link_libraries(zaro_scene PUBLIC zaro_core PRIVATE zaro_tomlc17)
zaro_target_defaults(zaro_scene)

add_library(zaro_script STATIC src/script/script.cpp)
target_link_libraries(zaro_script PUBLIC zaro_scene PRIVATE Luau.VM Luau.Compiler)
zaro_target_defaults(zaro_script)

add_library(zaro_render STATIC src/render/crowd_renderer.cpp)
target_link_libraries(zaro_render PUBLIC zaro_rhi)
zaro_target_defaults(zaro_render)

add_library(zaro_app STATIC src/app/crowd_app.cpp src/app/tools.cpp src/app/scene_tests.cpp)
target_link_libraries(zaro_app PUBLIC zaro_sim zaro_render zaro_scene zaro_script zaro_platform)
zaro_target_defaults(zaro_app)

# CLI. Everything an agent or the editor does goes through this binary.
add_executable(zaro src/cli/main.cpp)
target_link_libraries(zaro PRIVATE zaro_app)
zaro_target_defaults(zaro)
zaro_add_shaders(zaro shaders/crowd_cull.slang shaders/crowd_draw.slang)
add_custom_command(TARGET zaro POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:zaro_wgpu> $<TARGET_FILE_DIR:zaro>
  VERBATIM)

if(ZARO_BUILD_TESTS)
  enable_testing()

  # Public headers must stay valid C (plugin ABI).
  add_library(zaro_c_abi_check STATIC tests/c_abi_check.c)
  target_link_libraries(zaro_c_abi_check PRIVATE zaro_core)
  zaro_target_defaults(zaro_c_abi_check)

  add_test(NAME cli_version COMMAND zaro version)
  set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "^zaro [0-9]+\\.[0-9]+\\.[0-9]+ \\(abi [0-9]+\\)")

  add_test(NAME cli_unknown_command COMMAND zaro no-such-command)
  set_tests_properties(cli_unknown_command PROPERTIES WILL_FAIL TRUE)

  # Same state hash for 1 and many threads, and for repeated runs.
  add_executable(test_crowd_determinism tests/test_crowd_determinism.cpp)
  target_link_libraries(test_crowd_determinism PRIVATE zaro_sim)
  zaro_target_defaults(test_crowd_determinism)
  add_test(NAME crowd_determinism COMMAND test_crowd_determinism)

  # Scenes: prefab overrides, defaults, diagnostics with ids and lines.
  add_executable(test_scene tests/test_scene.cpp)
  target_link_libraries(test_scene PRIVATE zaro_scene zaro_sim)
  zaro_target_defaults(test_scene)
  add_test(NAME scene_loading COMMAND test_scene WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # Luau scripts: hooks, zaro API, errors with lines, sandbox, time budget, seeded random, reload.
  add_executable(test_script tests/test_script.cpp)
  target_link_libraries(test_script PRIVATE zaro_script zaro_sim)
  zaro_target_defaults(test_script)
  add_test(NAME scripting COMMAND test_script ${CMAKE_CURRENT_BINARY_DIR}
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # The example scene stays valid, and `zaro schema` stays valid JSON.
  add_test(NAME example_crowd_scene COMMAND zaro check examples/crowd/scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME schema_json
             COMMAND Python3::Interpreter -c "import json,subprocess,sys; d=json.loads(subprocess.run([sys.argv[1],'schema'],capture_output=True,check=True).stdout); assert d['components'], 'no components'"
                     $<TARGET_FILE:zaro>)
  endif()

  # A scene drives the run: its script raises the horde speed every 600 ticks.
  add_test(NAME example_crowd_scene_run
           COMMAND zaro run examples/crowd/scene.toml --sim-only --warmup 0 --frames 700 --enemies 2000
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(example_crowd_scene_run PROPERTIES PASS_REGULAR_EXPRESSION "script: wave 1 speed 4.5")
  # A failing script fails a headless run.
  add_test(NAME script_error_fails_run
           COMMAND zaro run tests/data/scripts/failing_scene.toml --sim-only --warmup 0 --frames 10
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(script_error_fails_run PROPERTIES WILL_FAIL TRUE)

  # Scene tests: the example passes (golden hash included), the broken ones fail.
  add_test(NAME scene_tests_example COMMAND zaro test examples/crowd/tests
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME scene_tests_failing COMMAND zaro test tests/data/scene_tests
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(scene_tests_failing PROPERTIES WILL_FAIL TRUE)
  add_test(NAME screenshot_gpu
           COMMAND zaro screenshot examples/crowd/scene.toml ${CMAKE_BINARY_DIR}/screenshot_test.png
                   --frames 10 --size 640x360
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_gpu PROPERTIES LABELS gpu)

  # No GPU needed: runs on CI runners.
  add_test(NAME crowd_sim_only COMMAND zaro run --sim-only --enemies 5000 --warmup 0 --frames 120)
  set_tests_properties(crowd_sim_only PROPERTIES PASS_REGULAR_EXPRESSION "state_hash=[0-9a-f]+ ticks=120")

  # Needs a GPU. Excluded on hosted CI with: ctest -LE gpu
  add_test(NAME crowd_headless_gpu
    COMMAND zaro run --headless --enemies 50000 --warmup 30 --frames 120 --size 1280x720
            --perf ${CMAKE_BINARY_DIR}/perf_smoke.json --screenshot ${CMAKE_BINARY_DIR}/smoke.png)
  set_tests_properties(crowd_headless_gpu PROPERTIES LABELS gpu)

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME lint COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/lint.py)
  endif()
endif()
