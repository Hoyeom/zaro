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
  src/core/version.cpp src/core/log.cpp src/core/jobs.cpp src/core/png.cpp src/core/png_decode.cpp
  src/core/reflect.cpp src/core/diag.cpp src/core/path.cpp)
target_include_directories(zaro_core PUBLIC include src)
target_link_libraries(zaro_core PRIVATE enkiTS zaro_stb_image)
# png_decode.cpp compiles the stb_image implementation; keep it out of the unity source.
set_source_files_properties(src/core/png_decode.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
if(ZARO_TRACY)
  target_link_libraries(zaro_core PUBLIC Tracy::TracyClient)
  target_compile_definitions(zaro_core PUBLIC ZARO_TRACY)
endif()
zaro_target_defaults(zaro_core)

add_library(zaro_platform STATIC src/platform/platform_sdl.cpp)
target_link_libraries(zaro_platform PUBLIC zaro_core PRIVATE SDL3::SDL3)
if(ANDROID)
  target_link_libraries(zaro_platform PRIVATE log android)
endif()
zaro_target_defaults(zaro_platform)

add_library(zaro_rhi STATIC src/rhi/rhi_wgpu.cpp)
target_link_libraries(zaro_rhi PUBLIC zaro_platform PRIVATE zaro_wgpu)
zaro_target_defaults(zaro_rhi)

add_library(zaro_sim STATIC src/sim/crowd.cpp src/sim/crowd_components.cpp src/sim/sprite_components.cpp)
target_link_libraries(zaro_sim PUBLIC zaro_core)
zaro_target_defaults(zaro_sim)

add_library(zaro_scene STATIC src/scene/scene.cpp src/scene/test_spec.cpp src/scene/strings.cpp src/scene/ui_state.cpp
  src/scene/theme.cpp src/scene/sprite_sheet.cpp)
target_link_libraries(zaro_scene PUBLIC zaro_core PRIVATE zaro_tomlc17)
zaro_target_defaults(zaro_scene)

add_library(zaro_script STATIC src/script/script.cpp)
target_link_libraries(zaro_script PUBLIC zaro_scene PRIVATE Luau.VM Luau.Compiler)
zaro_target_defaults(zaro_script)

add_library(zaro_render STATIC src/render/crowd_renderer.cpp src/render/ui_renderer.cpp src/render/sprite_renderer.cpp)
target_link_libraries(zaro_render PUBLIC zaro_rhi)
zaro_target_defaults(zaro_render)

add_library(zaro_ui STATIC
  src/ui/draw.cpp src/ui/font.cpp src/ui/text.cpp src/ui/pack.cpp src/ui/context.cpp
  src/ui/widgets.cpp src/ui/screen.cpp src/ui/navigator.cpp)
target_link_libraries(zaro_ui PUBLIC zaro_scene PRIVATE zaro_stb_truetype)
# font.cpp compiles the stb_truetype implementation; keep it out of the unity source.
set_source_files_properties(src/ui/font.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
zaro_target_defaults(zaro_ui)

add_library(zaro_app STATIC src/app/crowd_app.cpp src/app/tools.cpp src/app/scene_tests.cpp src/app/ui_layer.cpp
  src/app/sprite_layer.cpp src/app/export_android.cpp src/app/design_import.cpp)
target_link_libraries(zaro_app PUBLIC zaro_sim zaro_render zaro_scene zaro_script zaro_ui zaro_platform
  PRIVATE zaro_cjson)
zaro_target_defaults(zaro_app)

# CLI. Everything an agent or the editor does goes through this binary.
if(ANDROID)
  # SDLActivity loads libmain.so and calls its main() (ZaroActivity.getMainFunction, D-022).
  add_library(zaro SHARED src/cli/main.cpp)
  set_target_properties(zaro PROPERTIES OUTPUT_NAME main)
else()
  add_executable(zaro src/cli/main.cpp)
endif()
target_link_libraries(zaro PRIVATE zaro_app)
zaro_target_defaults(zaro)
zaro_add_shaders(zaro shaders/crowd_cull.slang shaders/crowd_draw.slang shaders/ui.slang shaders/sprite.slang)
zaro_add_fonts(zaro)
add_custom_command(TARGET zaro POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:zaro_wgpu> $<TARGET_FILE_DIR:zaro>
  VERBATIM)

if(ANDROID)
  include(ZaroAndroid)
endif()

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
  target_link_libraries(test_scene PRIVATE zaro_scene zaro_sim zaro_ui)
  zaro_target_defaults(test_scene)
  add_test(NAME scene_loading COMMAND test_scene WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # Luau scripts: hooks, zaro API, errors with lines, sandbox, time budget, seeded random, reload.
  add_executable(test_script tests/test_script.cpp)
  target_link_libraries(test_script PRIVATE zaro_script zaro_sim)
  zaro_target_defaults(test_script)
  add_test(NAME scripting COMMAND test_script ${CMAKE_CURRENT_BINARY_DIR}
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # Text: UTF-8, Korean and Latin glyphs, line breaking, the glyph atlas and its reset when full.
  add_executable(test_ui_text tests/test_ui_text.cpp)
  target_link_libraries(test_ui_text PRIVATE zaro_ui)
  zaro_target_defaults(test_ui_text)
  add_dependencies(test_ui_text zaro)  # the bundled fonts are copied when zaro builds
  add_test(NAME ui_text COMMAND test_ui_text ${CMAKE_BINARY_DIR}/fonts/)
  # Screens: layout, clicks, scrolling, modals, focus navigation, colour and enum fields.
  add_executable(test_ui_screen tests/test_ui_screen.cpp)
  target_link_libraries(test_ui_screen PRIVATE zaro_ui)
  zaro_target_defaults(test_ui_screen)
  add_dependencies(test_ui_screen zaro)
  add_test(NAME ui_screen COMMAND test_ui_screen ${CMAKE_BINARY_DIR}/fonts/)
  # Navigation policy: lifecycle, back in every state, claims, close, home, reset, keys, input.
  add_executable(test_ui_nav tests/test_ui_nav.cpp)
  target_link_libraries(test_ui_nav PRIVATE zaro_ui)
  zaro_target_defaults(test_ui_nav)
  add_test(NAME ui_navigation COMMAND test_ui_nav WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME check_open_action COMMAND zaro check tests/data/screens/nav/typo_scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_open_action PROPERTIES
    PASS_REGULAR_EXPRESSION "typo_root.toml:7:1: error\\[ZE0406\\]: node 'go': 'open:pupop' opens no screen of the scene's screens \\(did you mean 'open:popup'")
  # Text drawn on the GPU: the frame is written as PNG and must contain glyph pixels.
  add_executable(test_ui_render tests/test_ui_render.cpp)
  target_link_libraries(test_ui_render PRIVATE zaro_ui zaro_render)
  zaro_target_defaults(test_ui_render)
  add_dependencies(test_ui_render zaro)
  add_test(NAME ui_render_gpu COMMAND test_ui_render ${CMAKE_BINARY_DIR}/ ${CMAKE_BINARY_DIR}/ui_render_test.png)
  set_tests_properties(ui_render_gpu PROPERTIES LABELS gpu)

  # Sprite sheets: grid and explicit frames, animations, diagnostics with lines.
  add_executable(test_sprites tests/test_sprites.cpp)
  target_link_libraries(test_sprites PRIVATE zaro_scene)
  zaro_target_defaults(test_sprites)
  add_test(NAME sprite_sheets COMMAND test_sprites WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # Sprites drawn on the GPU: a depth-tested, mirrored billboard and a screen sprite.
  add_executable(test_sprite_render tests/test_sprite_render.cpp)
  target_link_libraries(test_sprite_render PRIVATE zaro_render)
  zaro_target_defaults(test_sprite_render)
  add_dependencies(test_sprite_render zaro)  # the shaders are built with zaro
  add_test(NAME sprite_render_gpu COMMAND test_sprite_render ${CMAKE_BINARY_DIR}/ ${CMAKE_BINARY_DIR}/sprite_render_test.png
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(sprite_render_gpu PROPERTIES LABELS gpu)
  # A scene with sprites stays valid; a misspelt animation is reported at its line.
  add_test(NAME check_sprite_scene COMMAND zaro check tests/data/sprites/scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME check_sprite_animation COMMAND zaro check tests/data/sprites/typo_scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_sprite_animation PROPERTIES
    PASS_REGULAR_EXPRESSION "typo_scene.toml:9:1: error\\[ZE0902\\]: entity 'blob': sheet '[^']*blob.toml' has no animation 'idel' \\(did you mean 'idle'")
  add_test(NAME scene_tests_sprites COMMAND zaro test tests/data/sprites
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME screenshot_sprites
           COMMAND zaro screenshot tests/data/sprites/scene.toml ${CMAKE_BINARY_DIR}/sprites.png --frames 5 --size 640x360
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_sprites PROPERTIES LABELS gpu)

  # The example scene stays valid, and `zaro schema` stays valid JSON.
  add_test(NAME example_crowd_scene COMMAND zaro check examples/crowd/scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # The hello scene with its screens and string tables stays valid; a broken one reports its image.
  add_test(NAME example_hello_scene COMMAND zaro check examples/hello/scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # The committed hello theme is what its design kit imports to.
  add_test(NAME example_hello_design_kit
           COMMAND zaro import-design examples/hello/design examples/hello/ui/kit --check
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME check_screen_keys COMMAND zaro check tests/data/screens/keys_scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_screen_keys PROPERTIES
    PASS_REGULAR_EXPRESSION "keys.toml:22:1: error\\[ZE0701\\]: node 'icon': cannot open image")
  # The HUD in both languages, and the settings popup, rendered over the arena (needs a GPU).
  foreach(lang ko en)
    add_test(NAME screenshot_hello_${lang}
             COMMAND zaro screenshot examples/hello/scene.toml ${CMAKE_BINARY_DIR}/hello_${lang}.png
                     --frames 3 --size 1280x720 --lang ${lang}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(screenshot_hello_${lang} PROPERTIES LABELS gpu)
    add_test(NAME screenshot_hello_settings_${lang}
             COMMAND zaro screenshot examples/hello/scene.toml ${CMAKE_BINARY_DIR}/hello_settings_${lang}.png
                     --frames 15 --size 1280x720 --lang ${lang} --click hud/settings_button
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(screenshot_hello_settings_${lang} PROPERTIES LABELS gpu)
  endforeach()
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
  add_test(NAME scene_tests_example COMMAND zaro test examples
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
