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

add_library(zaro_sim STATIC src/sim/crowd.cpp src/sim/crowd_components.cpp src/sim/sprite_components.cpp
            src/sim/fx_components.cpp src/sim/shape_components.cpp)
target_link_libraries(zaro_sim PUBLIC zaro_core)
zaro_target_defaults(zaro_sim)

add_library(zaro_scene STATIC src/scene/scene.cpp src/scene/world.cpp src/scene/tween.cpp src/scene/test_spec.cpp src/scene/strings.cpp src/scene/ui_state.cpp
  src/scene/theme.cpp src/scene/sprite_sheet.cpp src/scene/build_extension.cpp
  src/scene/build_spec.cpp src/scene/sound_bank.cpp src/scene/audio_state.cpp src/scene/save_spec.cpp)
target_link_libraries(zaro_scene PUBLIC zaro_core PRIVATE zaro_tomlc17)
zaro_target_defaults(zaro_scene)

add_library(zaro_script STATIC src/script/script.cpp)
target_link_libraries(zaro_script PUBLIC zaro_scene PRIVATE Luau.VM Luau.Compiler)
zaro_target_defaults(zaro_script)

add_library(zaro_render STATIC src/render/crowd_renderer.cpp src/render/ui_renderer.cpp src/render/sprite_renderer.cpp
            src/render/post_renderer.cpp src/render/shape_renderer.cpp src/render/particle_renderer.cpp)
target_link_libraries(zaro_render PUBLIC zaro_rhi)
zaro_target_defaults(zaro_render)

# Cook-time asset tools (SVG art into sprite atlases, D-028). Only the zaro tool links them; the
# Android runtime is built without, so no game binary contains the SVG renderer.
if(NOT ANDROID)
  add_library(zaro_cook STATIC src/cook/svg.cpp)
  target_link_libraries(zaro_cook PUBLIC zaro_scene PRIVATE zaro_lunasvg)
  zaro_target_defaults(zaro_cook)
endif()

# Sound mixing on miniaudio (D-032); the output device is platform's.
add_library(zaro_audio STATIC src/audio/audio.cpp)
target_link_libraries(zaro_audio PUBLIC zaro_core PRIVATE zaro_miniaudio)
zaro_target_defaults(zaro_audio)

add_library(zaro_ui STATIC
  src/ui/draw.cpp src/ui/font.cpp src/ui/text.cpp src/ui/pack.cpp src/ui/context.cpp
  src/ui/widgets.cpp src/ui/screen.cpp src/ui/navigator.cpp)
target_link_libraries(zaro_ui PUBLIC zaro_scene PRIVATE zaro_stb_truetype)
# font.cpp compiles the stb_truetype implementation; keep it out of the unity source.
set_source_files_properties(src/ui/font.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
zaro_target_defaults(zaro_ui)

add_library(zaro_app STATIC src/app/crowd_app.cpp src/app/audio_layer.cpp src/app/save_layer.cpp src/app/tools.cpp src/app/scene_tests.cpp src/app/ui_layer.cpp
  src/app/sprite_layer.cpp src/app/export_android.cpp src/app/design_import.cpp src/app/pipeline.cpp
  src/app/fx_layer.cpp src/app/shape_layer.cpp)
target_link_libraries(zaro_app PUBLIC zaro_sim zaro_render zaro_scene zaro_script zaro_ui zaro_audio zaro_platform
  PRIVATE zaro_cjson)
if(TARGET zaro_cook)
  target_link_libraries(zaro_app PRIVATE zaro_cook)
  target_compile_definitions(zaro_app PRIVATE ZARO_HAS_COOK=1)
endif()
zaro_target_defaults(zaro_app)
# zaro build's target stages run this checkout's tools/build.py and tools/android.py (D-027).
if(NOT ANDROID)
  find_package(Python3 COMPONENTS Interpreter)
  target_compile_definitions(zaro_app PRIVATE ZARO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                                              ZARO_PYTHON="${Python3_EXECUTABLE}")
endif()

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
zaro_add_shaders(zaro shaders/crowd_cull.slang shaders/crowd_draw.slang shaders/ui.slang shaders/sprite.slang
                 shaders/post.slang shaders/shape.slang shaders/particles_sim.slang shaders/particles_draw.slang)
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

  # Audio (D-032): sound files, synthesis, the request queue, the mixer without a device, audio.*.
  add_executable(test_audio tests/test_audio.cpp)
  target_link_libraries(test_audio PRIVATE zaro_audio zaro_script zaro_sim)
  zaro_target_defaults(test_audio)
  add_test(NAME audio COMMAND test_audio WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME check_audio_scene COMMAND zaro check tests/data/audio/scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME check_audio_typos COMMAND zaro check tests/data/audio/typo_scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_audio_typos PROPERTIES
    PASS_REGULAR_EXPRESSION "typo_scene.toml:6:1: error\\[ZE1302\\]: music: [^ ]*sounds.toml has no sound 'thme' \\(did you mean 'theme'.*typo_panel.toml:8:1: error\\[ZE1302\\]: node 'ok': [^ ]*sounds.toml has no sound 'clik' \\(did you mean 'click'")
  add_test(NAME scene_tests_audio COMMAND zaro test tests/data/audio/tests
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # Local saves (D-033): declarations, migrations, damaged and newer saves, the file store, save.*.
  add_executable(test_save tests/test_save.cpp)
  target_link_libraries(test_save PRIVATE zaro_app)
  zaro_target_defaults(test_save)
  add_test(NAME save COMMAND test_save ${CMAKE_CURRENT_BINARY_DIR} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME check_save_scene COMMAND zaro check tests/data/save/scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME scene_tests_save COMMAND zaro test tests/data/save/tests
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # Across runs: the second run starts from what the first one committed.
  add_test(NAME save_runs_clean COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_CURRENT_BINARY_DIR}/save_runs)
  add_test(NAME save_runs_first COMMAND zaro run tests/data/save/scene.toml --sim-only --warmup 0 --frames 130
           --save-dir ${CMAKE_CURRENT_BINARY_DIR}/save_runs WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME save_runs_second COMMAND zaro run tests/data/save/scene.toml --sim-only --warmup 0 --frames 10
           --save-dir ${CMAKE_CURRENT_BINARY_DIR}/save_runs WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(save_runs_clean PROPERTIES FIXTURES_SETUP save_runs)
  set_tests_properties(save_runs_first PROPERTIES FIXTURES_REQUIRED save_runs FIXTURES_SETUP save_runs_first
    PASS_REGULAR_EXPRESSION "script: best 0 coins 0")
  set_tests_properties(save_runs_second PROPERTIES FIXTURES_REQUIRED "save_runs;save_runs_first"
    PASS_REGULAR_EXPRESSION "script: best 2 coins 0")

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
  # The HDR frame and bloom (D-030): glow beside a bright sprite, in each renderable HDR format.
  add_executable(test_post_render tests/test_post_render.cpp)
  target_link_libraries(test_post_render PRIVATE zaro_render)
  zaro_target_defaults(test_post_render)
  add_dependencies(test_post_render zaro)  # the shaders are built with zaro
  add_test(NAME post_render_gpu COMMAND test_post_render ${CMAKE_BINARY_DIR}/ ${CMAKE_BINARY_DIR}/post_render_test.png
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(post_render_gpu PROPERTIES LABELS gpu)
  # Shapes: every kind filled and outlined, a halo, and one on the ground of a 3D view.
  add_executable(test_shape_render tests/test_shape_render.cpp)
  target_link_libraries(test_shape_render PRIVATE zaro_render)
  zaro_target_defaults(test_shape_render)
  add_dependencies(test_shape_render zaro)  # the shaders are built with zaro
  add_test(NAME shape_render_gpu COMMAND test_shape_render ${CMAKE_BINARY_DIR}/ ${CMAKE_BINARY_DIR}/shape_render_test.png)
  set_tests_properties(shape_render_gpu PROPERTIES LABELS gpu)
  # Particles: a burst emitted on the GPU, then moved by a frame's time.
  add_executable(test_particle_render tests/test_particle_render.cpp)
  target_link_libraries(test_particle_render PRIVATE zaro_render)
  zaro_target_defaults(test_particle_render)
  add_dependencies(test_particle_render zaro)  # the shaders are built with zaro
  add_test(NAME particle_render_gpu COMMAND test_particle_render ${CMAKE_BINARY_DIR}/
           ${CMAKE_BINARY_DIR}/particle_render_test.png)
  set_tests_properties(particle_render_gpu PROPERTIES LABELS gpu)
  # A scene with sprites stays valid; a misspelt animation is reported at its line.
  add_test(NAME check_sprite_scene COMMAND zaro check tests/data/sprites/scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME check_sprite_animation COMMAND zaro check tests/data/sprites/typo_scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_sprite_animation PROPERTIES
    PASS_REGULAR_EXPRESSION "typo_scene.toml:9:1: error\\[ZE0902\\]: entity 'blob': sheet '[^']*blob.toml' has no animation 'idel' \\(did you mean 'idle'")
  add_test(NAME check_crowd_sprite_animation COMMAND zaro check tests/data/sprites/crowd_typo.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_crowd_sprite_animation PROPERTIES
    PASS_REGULAR_EXPRESSION "crowd_typo.toml:11:1: error\\[ZE0902\\]: entity 'horde': sheet '[^']*' has no animation 'lnad' \\(did you mean 'land'")
  # Spawnable prefabs (D-031): each needs a [prefab] name, unique in the scene.
  add_test(NAME check_spawn_prefabs COMMAND zaro check tests/data/scripts/bad_prefabs_scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_spawn_prefabs PROPERTIES
    PASS_REGULAR_EXPRESSION "unnamed.toml:1:1: error\\[ZE1200\\]: a prefab in \\[scene\\] prefabs needs a name.*bad_prefabs_scene.toml:4:1: error\\[ZE1200\\]: prefabs '[^']*grunt.toml' and '[^']*grunt_again.toml' are both named 'grunt'")
  # Scene entities may have parents (D-031), which must come earlier in the file.
  add_test(NAME check_entity_parents COMMAND zaro check tests/data/scripts/bad_parent_scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_entity_parents PROPERTIES
    PASS_REGULAR_EXPRESSION "bad_parent_scene.toml:10:11: error\\[ZE0408\\]: parent 'planet' must come before entity 'moon'.*bad_parent_scene.toml:14:11: error\\[ZE0408\\]: unknown parent entity 'snu' \\(did you mean 'sun'")
  # Tweens change only visual fields; their tables are checked like components (D-031).
  add_test(NAME check_tweens COMMAND zaro check tests/data/scripts/bad_tween_scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_tweens PROPERTIES
    PASS_REGULAR_EXPRESSION "bad_tween_scene.toml:11:14: error\\[ZE1202\\]: Transform.rotation is gameplay state.*bad_tween_scene.toml:20:9: error\\[ZE1202\\]: unknown tween ease 'out_cubc' \\(did you mean 'out_cubic'.*bad_tween_scene.toml:21:9: error\\[ZE1202\\]: unexpected tween key 'speed'")
  add_test(NAME scene_tests_sprites COMMAND zaro test tests/data/sprites
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME screenshot_sprites
           COMMAND zaro screenshot tests/data/sprites/scene.toml ${CMAKE_BINARY_DIR}/sprites.png --frames 5 --size 640x360
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_sprites PROPERTIES LABELS gpu)
  # A scene with Bloom: valid, and it renders through the HDR frame.
  add_test(NAME check_bloom_scene COMMAND zaro check tests/data/fx/bloom.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME screenshot_bloom
           COMMAND zaro screenshot tests/data/fx/bloom.toml ${CMAKE_BINARY_DIR}/bloom.png --frames 5 --size 640x360
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_bloom PROPERTIES LABELS gpu)
  # Shapes: a scene with every kind, flash on the tick clock, ZE1100 for invalid shape components.
  add_test(NAME check_shapes_scene COMMAND zaro check tests/data/fx/shapes.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME scene_tests_fx COMMAND zaro test tests/data/fx
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # Scripts and the crowd (D-031): zaro.crowd_kill, crowd_hit events, the player's Transform.
  add_test(NAME scene_tests_gameplay COMMAND zaro test tests/data/gameplay
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # The crowd's size from a script (D-034): growing, clamped at max, retired; a count above max.
  add_test(NAME scene_tests_crowd COMMAND zaro test tests/data/crowd
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME check_crowd_count_max COMMAND zaro check tests/data/crowd/count_above_max.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_crowd_count_max PROPERTIES
    PASS_REGULAR_EXPRESSION "count_above_max.toml:5:1: error\\[ZE1400\\]: entity 'horde': CrowdSpawner count 500 is above its max 400")
  # Kinds of enemy (D-036): more spawners than kinds, and two kinds with their own looks.
  add_test(NAME check_crowd_too_many_kinds COMMAND zaro check tests/data/crowd/too_many_kinds.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_crowd_too_many_kinds PROPERTIES
    PASS_REGULAR_EXPRESSION "too_many_kinds.toml:45:1: error\\[ZE1400\\]: entity 'kind9': a crowd has at most 8 kinds")
  add_test(NAME screenshot_crowd_kinds
           COMMAND zaro screenshot tests/data/crowd/kinds.toml ${CMAKE_BINARY_DIR}/crowd_kinds.png --frames 120 --size 960x540
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_crowd_kinds PROPERTIES LABELS gpu)
  add_test(NAME check_crowd_two_looks COMMAND zaro check tests/data/fx/crowd_twice.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_crowd_two_looks PROPERTIES
    PASS_REGULAR_EXPRESSION "crowd_twice.toml:12:1: error\\[ZE1100\\]: entity 'look': the crowd has both CrowdShape and CrowdSprite \\(on 'horde'\\)")
  add_test(NAME check_screen_shape_follow COMMAND zaro check tests/data/fx/screen_follow.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(check_screen_shape_follow PROPERTIES
    PASS_REGULAR_EXPRESSION "screen_follow.toml:9:1: error\\[ZE1100\\]: entity 'marker': a screen shape cannot follow the player")
  add_test(NAME screenshot_shapes
           COMMAND zaro screenshot tests/data/fx/shapes.toml ${CMAKE_BINARY_DIR}/shapes.png --frames 30 --size 640x360
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_shapes PROPERTIES LABELS gpu)
  # The scene camera (D-036): its projection without a GPU, and a portrait screenshot.
  add_executable(test_camera tests/test_camera.cpp)
  target_link_libraries(test_camera PRIVATE zaro_render)
  zaro_target_defaults(test_camera)
  add_test(NAME scene_camera COMMAND test_camera)
  add_test(NAME check_camera_scene COMMAND zaro check tests/data/fx/camera.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME screenshot_camera
           COMMAND zaro screenshot tests/data/fx/camera.toml ${CMAKE_BINARY_DIR}/camera.png --frames 60 --size 540x960
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_camera PROPERTIES LABELS gpu)
  add_test(NAME check_camera_flat_scene COMMAND zaro check tests/data/fx/camera_flat.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME screenshot_camera_flat
           COMMAND zaro screenshot tests/data/fx/camera_flat.toml ${CMAKE_BINARY_DIR}/camera_flat.png --frames 60
           --size 540x960 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_camera_flat PROPERTIES LABELS gpu)
  # Open ground with dots instead of a grid (D-039).
  add_test(NAME check_arena_flat_scene COMMAND zaro check tests/data/fx/arena_flat.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME screenshot_arena_flat
           COMMAND zaro screenshot tests/data/fx/arena_flat.toml ${CMAKE_BINARY_DIR}/arena_flat.png --frames 60
           --size 540x960 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_arena_flat PROPERTIES LABELS gpu)
  # Particles, shake and flash: sparks from hits and emitters through the HDR frame.
  add_test(NAME check_sparks_scene COMMAND zaro check tests/data/fx/sparks.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME screenshot_sparks
           COMMAND zaro screenshot tests/data/fx/sparks.toml ${CMAKE_BINARY_DIR}/sparks.png --frames 30 --size 640x360
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_sparks PROPERTIES LABELS gpu)
  # Solid pieces instead of sparks (D-040).
  add_test(NAME check_pieces_scene COMMAND zaro check tests/data/fx/pieces.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME screenshot_pieces
           COMMAND zaro screenshot tests/data/fx/pieces.toml ${CMAKE_BINARY_DIR}/pieces.png --frames 40 --size 540x960
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_pieces PROPERTIES LABELS gpu)

  # SVG cook: classes, clip paths, masks, <use> and gradients drawn; text and filters reported.
  if(TARGET zaro_cook)
    add_executable(test_svg tests/test_svg.cpp)
    target_link_libraries(test_svg PRIVATE zaro_cook)
    zaro_target_defaults(test_svg)
    add_test(NAME svg_cook COMMAND test_svg ${CMAKE_BINARY_DIR} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    # The cats' SVG art cooks into its atlas (no GPU), and art the cook cannot draw fails `zaro check`.
    add_test(NAME example_cats_cook COMMAND zaro cook examples/cats/scene.toml --force
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(example_cats_cook PROPERTIES
      PASS_REGULAR_EXPRESSION "cooked examples/cats/.zaro-cache/cat.png \\(8 frames\\)")
    # The build pipeline's cook-svg step cooks the game's SVG sheets in the pre stage (D-027, D-028).
    add_test(NAME example_cats_build COMMAND zaro build examples/cats/build.toml --out ${CMAKE_BINARY_DIR}/zaro-build/cats
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(example_cats_build PROPERTIES DEPENDS example_cats_cook
      PASS_REGULAR_EXPRESSION "examples/cats/scene.toml: [0-9]+ sheet\\(s\\) cooked, [0-9]+ up to date")
    add_test(NAME check_svg_art COMMAND zaro check tests/data/svg/bad_scene.toml
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(check_svg_art PROPERTIES
      PASS_REGULAR_EXPRESSION "unsupported.svg:7:1: error\\[ZE0903\\]: <text> is not drawn by the SVG cook")
  endif()
  # The cats scene with SVG sprites stays valid; it renders in the 3D view and on the screen.
  add_test(NAME example_cats_scene COMMAND zaro check examples/cats/scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME screenshot_cats
           COMMAND zaro screenshot examples/cats/scene.toml ${CMAKE_BINARY_DIR}/cats.png --frames 30 --size 960x540
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_cats PROPERTIES LABELS gpu)
  # The neon arena (P6): its look stays valid, its effects run on the tick clock, and it renders.
  add_test(NAME example_neon_scene COMMAND zaro check examples/neon/scene.toml
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME example_neon_tests COMMAND zaro test examples/neon/tests
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME screenshot_neon
           COMMAND zaro screenshot examples/neon/scene.toml ${CMAKE_BINARY_DIR}/neon.png --frames 200 --size 960x540
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_neon PROPERTIES LABELS gpu)
  add_test(NAME screenshot_neon_settings
           COMMAND zaro screenshot examples/neon/scene.toml ${CMAKE_BINARY_DIR}/neon_settings.png --frames 30
           --size 960x540 --click hud/settings_button WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(screenshot_neon_settings PROPERTIES LABELS gpu)
  # Its best score across runs (D-033): the first wave (tick 480) commits it, the next run starts from it.
  add_test(NAME example_neon_save_clean COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_CURRENT_BINARY_DIR}/neon_save)
  add_test(NAME example_neon_save_first COMMAND zaro run examples/neon/scene.toml --sim-only --warmup 0 --frames 500
           --save-dir ${CMAKE_CURRENT_BINARY_DIR}/neon_save WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME example_neon_save_second COMMAND zaro run examples/neon/scene.toml --sim-only --warmup 0 --frames 1
           --save-dir ${CMAKE_CURRENT_BINARY_DIR}/neon_save WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(example_neon_save_clean PROPERTIES FIXTURES_SETUP neon_save)
  set_tests_properties(example_neon_save_first PROPERTIES FIXTURES_REQUIRED neon_save FIXTURES_SETUP neon_save_first
    PASS_REGULAR_EXPRESSION "script: best score 0")
  set_tests_properties(example_neon_save_second PROPERTIES FIXTURES_REQUIRED "neon_save;neon_save_first"
    PASS_REGULAR_EXPRESSION "script: best score 435")

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
    # zaro build without a target: stages, step order, stamping, file:line errors (D-027).
    add_test(NAME build_pipeline
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_build_pipeline.py $<TARGET_FILE:zaro>
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    # The Android export (version, build extensions, their errors) without an Android build.
    add_test(NAME android_export
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_android_export.py $<TARGET_FILE:zaro>
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
endif()
