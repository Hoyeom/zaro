# Third-party dependencies, pinned by version and SHA-256 (D-004, D-012 licenses).
#
# Downloads and sources are cached in .cache/deps so every preset shares them. Prebuilt binaries
# are used where the upstream project ships them (wgpu-native, Slang); the rest builds from source.
# Dependencies built from source get their binary directory inside the preset's build tree
# (_ZARO_DEP_BIN), so presets with different compilers or flags never share objects.

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.cache/deps" CACHE PATH "" FORCE)
set(FETCHCONTENT_QUIET OFF)
set(_ZARO_DEP_BIN ${CMAKE_BINARY_DIR}/_deps)

# Host: the machine that builds (Slang runs here). Target: where zaro runs (wgpu-native, SDL).
# They differ only when cross-compiling for Android (D-022).
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
  set(_zaro_host windows-x86_64)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  set(_zaro_host macos-aarch64)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
  set(_zaro_host linux-x86_64)
else()
  message(FATAL_ERROR "zaro: unsupported host ${CMAKE_HOST_SYSTEM_NAME}/${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()
if(ANDROID)
  if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "zaro: Android builds support arm64-v8a only, got ${ANDROID_ABI}")
  endif()
  set(_zaro_target android-aarch64)
elseif(CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "zaro: unsupported target ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
else()
  set(_zaro_target ${_zaro_host})
endif()

# --- wgpu-native v29.0.1.1 (MIT OR Apache-2.0), prebuilt -------------------------------------
set(_wgpu_base https://github.com/gfx-rs/wgpu-native/releases/download/v29.0.1.1)
if(_zaro_target STREQUAL windows-x86_64)
  set(_wgpu_url ${_wgpu_base}/wgpu-windows-x86_64-msvc-release.zip)
  set(_wgpu_sha 7e67d7445c42aeb85e30f88930fd8d7d83ee769e3390aeb1ada75ebf3cf78132)
elseif(_zaro_target STREQUAL macos-aarch64)
  set(_wgpu_url ${_wgpu_base}/wgpu-macos-aarch64-release.zip)
  set(_wgpu_sha a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e)
elseif(_zaro_target STREQUAL android-aarch64)
  set(_wgpu_url ${_wgpu_base}/wgpu-android-aarch64-release.zip)
  set(_wgpu_sha 721741f1b05a20c1738166bedf7a5efb2ba4b382da689526d3fc33de22bdd573)
else()
  set(_wgpu_url ${_wgpu_base}/wgpu-linux-x86_64-release.zip)
  set(_wgpu_sha 95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a)
endif()
# The Android archive gets its own name, so it never replaces the desktop one in .cache/deps.
if(ANDROID)
  set(_wgpu_dep wgpu_android)
else()
  set(_wgpu_dep wgpu)
endif()
FetchContent_Declare(${_wgpu_dep} URL ${_wgpu_url} URL_HASH SHA256=${_wgpu_sha})
FetchContent_MakeAvailable(${_wgpu_dep})
set(wgpu_SOURCE_DIR ${${_wgpu_dep}_SOURCE_DIR})

add_library(zaro_wgpu SHARED IMPORTED GLOBAL)
target_include_directories(zaro_wgpu INTERFACE ${wgpu_SOURCE_DIR}/include)
if(WIN32)
  set_target_properties(zaro_wgpu PROPERTIES
    IMPORTED_LOCATION ${wgpu_SOURCE_DIR}/lib/wgpu_native.dll
    IMPORTED_IMPLIB ${wgpu_SOURCE_DIR}/lib/wgpu_native.dll.lib)
elseif(APPLE)
  set_target_properties(zaro_wgpu PROPERTIES IMPORTED_LOCATION ${wgpu_SOURCE_DIR}/lib/libwgpu_native.dylib)
elseif(ANDROID)
  # The library has no SONAME. Linking it by name records NEEDED libwgpu_native.so instead of
  # this machine's path, so the device's linker finds it next to libmain.so.
  set_target_properties(zaro_wgpu PROPERTIES
    IMPORTED_LOCATION ${wgpu_SOURCE_DIR}/lib/libwgpu_native.so
    IMPORTED_NO_SONAME TRUE)
else()
  set_target_properties(zaro_wgpu PROPERTIES IMPORTED_LOCATION ${wgpu_SOURCE_DIR}/lib/libwgpu_native.so)
endif()

# --- Slang v2026.20 (Apache-2.0 WITH LLVM-exception), prebuilt compiler for build time ----------
set(_slang_base https://github.com/shader-slang/slang/releases/download/v2026.20)
if(_zaro_host STREQUAL windows-x86_64)
  set(_slang_url ${_slang_base}/slang-2026.20-windows-x86_64.zip)
  set(_slang_sha e6dd32bbddf1720ebb4e679d55e9635ce792865a5bd84f3d2cd9af28647c0174)
elseif(_zaro_host STREQUAL macos-aarch64)
  set(_slang_url ${_slang_base}/slang-2026.20-macos-aarch64.tar.gz)
  set(_slang_sha 07fa8833fdbb343c3674c84fdd885910f495f3daaaa0c732b32980151e8b58c4)
else()
  set(_slang_url ${_slang_base}/slang-2026.20-linux-x86_64.tar.gz)
  set(_slang_sha e863e568c204810d6e8bc31852f056aef6763c55ea9925db2db7affa060347c9)
endif()
# SOURCE_SUBDIR points at a directory without CMakeLists.txt, so this only downloads.
FetchContent_Declare(slang URL ${_slang_url} URL_HASH SHA256=${_slang_sha} SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(slang)
find_program(ZARO_SLANGC slangc PATHS ${slang_SOURCE_DIR}/bin NO_DEFAULT_PATH REQUIRED)

# --- SDL 3.4.18 (zlib), only the subsystems ZARO uses -------------------------------------------
# Static on desktop. Android uses libSDL3.so, which SDLActivity loads before libmain.so (D-022).
if(ANDROID)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3 SYSTEM BINARY_DIR ${_ZARO_DEP_BIN}/sdl3-build
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
  URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
FetchContent_MakeAvailable(SDL3)

# --- enkiTS v1.12 (zlib) -------------------------------------------------------------------------
set(ENKITS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_C_INTERFACE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(enkiTS SYSTEM BINARY_DIR ${_ZARO_DEP_BIN}/enkits-build
  URL https://github.com/dougbinks/enkiTS/archive/refs/tags/v1.12.tar.gz
  URL_HASH SHA256=8373b6199d56816bd3ba58432eae74e2ebd5afcfdffb723073cc34730b189fd5)
FetchContent_MakeAvailable(enkiTS)
target_include_directories(enkiTS SYSTEM INTERFACE ${enkits_SOURCE_DIR}/src)

# --- tomlc17 R261003 (MIT), TOML 1.1 parser with line/column on every value ------------------------
# The repository has no CMake build; compile its single C file as our own static library.
FetchContent_Declare(tomlc17
  URL https://github.com/cktan/tomlc17/archive/refs/tags/R261003.tar.gz
  URL_HASH SHA256=48876f14bd7a123f95f16074dd0d5681f75c397c1bce61f1a52f42201f90eacc
  SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(tomlc17)
add_library(zaro_tomlc17 STATIC ${tomlc17_SOURCE_DIR}/src/tomlc17.c)
target_include_directories(zaro_tomlc17 SYSTEM PUBLIC ${tomlc17_SOURCE_DIR}/src)
set_target_properties(zaro_tomlc17 PROPERTIES C_STANDARD 17)
# The source contains UTF-8 characters; MSVC otherwise reads it in the ANSI code page (C4819).
target_compile_options(zaro_tomlc17 PRIVATE $<$<C_COMPILER_ID:MSVC>:/utf-8>)

# --- Luau 0.743 (MIT), sandboxed game scripts (D-009) ---------------------------------------------
# Only the VM and the bytecode compiler; no CLI, tests, analysis or JIT.
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# EXCLUDE_FROM_ALL: only the targets ZARO links (VM, Compiler and their deps) are built.
FetchContent_Declare(luau SYSTEM EXCLUDE_FROM_ALL BINARY_DIR ${_ZARO_DEP_BIN}/luau-build
  URL https://github.com/luau-lang/luau/archive/refs/tags/0.743.tar.gz
  URL_HASH SHA256=be23707d541710a978f2c559c069e22ba9c7c449e07d60472fb4e074914bc91d)
FetchContent_MakeAvailable(luau)
# Script errors unwind with longjmp, never as C++ exceptions through ZARO's no-exception code.
target_compile_definitions(Luau.VM PRIVATE LUA_USE_LONGJMP=1)
# The compiler throws and catches internally (luau_compile never lets one escape), so the Luau
# targets get exceptions back; CMakeLists.txt removes /EHsc globally for ZARO code.
foreach(t Luau.Common Luau.Ast Luau.Compiler Luau.VM)
  if(TARGET ${t})
    target_compile_options(${t} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc /utf-8>)
  endif()
endforeach()

# --- stb_truetype 1.26 (public domain or MIT), font rasterizer for the ui module (D-019) ----------
# One header, pinned by commit; src/ui/font.cpp holds the implementation.
FetchContent_Declare(stb_truetype
  URL https://raw.githubusercontent.com/nothings/stb/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5/stb_truetype.h
  URL_HASH SHA256=ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab
  DOWNLOAD_NO_EXTRACT TRUE SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(stb_truetype)
add_library(zaro_stb_truetype INTERFACE)
target_include_directories(zaro_stb_truetype SYSTEM INTERFACE ${stb_truetype_SOURCE_DIR})

# --- stb_image 2.30 (public domain or MIT), PNG decoding for UI images -----------------------------
# Same pinned commit; src/ui/image.cpp holds the implementation (PNG only).
FetchContent_Declare(stb_image
  URL https://raw.githubusercontent.com/nothings/stb/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5/stb_image.h
  URL_HASH SHA256=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
  DOWNLOAD_NO_EXTRACT TRUE SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(stb_image)
add_library(zaro_stb_image INTERFACE)
target_include_directories(zaro_stb_image SYSTEM INTERFACE ${stb_image_SOURCE_DIR})

# --- cJSON 1.7.19 (MIT), reads design-kit tokens for `zaro import-design` (D-022) ---------------
# Only its single C file; src/app/design_import.cpp is the one file that includes cJSON.h.
FetchContent_Declare(cjson
  URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
  URL_HASH SHA256=7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562
  SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(cjson)
add_library(zaro_cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
target_include_directories(zaro_cjson SYSTEM PUBLIC ${cjson_SOURCE_DIR})
set_target_properties(zaro_cjson PROPERTIES C_STANDARD 17)

# --- Noto Sans KR (SIL OFL 1.1), the bundled default font (D-019) --------------------------------
# Unmodified OTF files from notofonts/noto-cjk, pinned by commit. Copied to <bin>/fonts/.
set(_noto_base https://raw.githubusercontent.com/notofonts/noto-cjk/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans)
FetchContent_Declare(noto_sans_kr_regular
  URL ${_noto_base}/SubsetOTF/KR/NotoSansKR-Regular.otf
  URL_HASH SHA256=69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68
  DOWNLOAD_NO_EXTRACT TRUE SOURCE_SUBDIR _none)
FetchContent_Declare(noto_sans_kr_bold
  URL ${_noto_base}/SubsetOTF/KR/NotoSansKR-Bold.otf
  URL_HASH SHA256=5a6ceb287ed2fc6cfc6213144ebea68cbd94b20fc9eb873d8486493bf02d9bda
  DOWNLOAD_NO_EXTRACT TRUE SOURCE_SUBDIR _none)
FetchContent_Declare(noto_sans_kr_license
  URL ${_noto_base}/LICENSE
  URL_HASH SHA256=6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2
  DOWNLOAD_NO_EXTRACT TRUE SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(noto_sans_kr_regular noto_sans_kr_bold noto_sans_kr_license)
set(ZARO_FONT_FILES
  ${noto_sans_kr_regular_SOURCE_DIR}/NotoSansKR-Regular.otf
  ${noto_sans_kr_bold_SOURCE_DIR}/NotoSansKR-Bold.otf)
set(ZARO_FONT_LICENSE ${noto_sans_kr_license_SOURCE_DIR}/LICENSE)

# Copies the bundled fonts and their license to <bin>/fonts/ after `target` builds.
function(zaro_add_fonts target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ZARO_FONT_FILES} ${CMAKE_BINARY_DIR}/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ZARO_FONT_LICENSE} ${CMAKE_BINARY_DIR}/fonts/NotoSansKR-LICENSE.txt
    VERBATIM)
endfunction()

# --- Tracy v0.14.1 (BSD-3), only with ZARO_TRACY ---------------------------------------------------
if(ZARO_TRACY)
  set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
  set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
  FetchContent_Declare(tracy SYSTEM BINARY_DIR ${_ZARO_DEP_BIN}/tracy-build
    URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
    URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1)
  FetchContent_MakeAvailable(tracy)
endif()

# Compile Slang shaders to WGSL next to the executable: <bin>/shaders/<name>.wgsl
function(zaro_add_shaders target)
  # Every shader depends on all .slang files so edits to imported modules rebuild it.
  # (Evaluated at configure time; a new .slang file is added to the call below anyway.)
  file(GLOB _zaro_shader_deps ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang)
  set(outputs)
  foreach(src ${ARGN})
    get_filename_component(name ${src} NAME_WE)
    set(out ${CMAKE_BINARY_DIR}/shaders/${name}.wgsl)
    add_custom_command(
      OUTPUT ${out}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
      COMMAND ${ZARO_SLANGC} ${CMAKE_CURRENT_SOURCE_DIR}/${src} -target wgsl -warnings-as-errors all -o ${out}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${_zaro_shader_deps}
      COMMENT "slangc ${src}"
      VERBATIM)
    list(APPEND outputs ${out})
  endforeach()
  add_custom_target(${target}_shaders DEPENDS ${outputs})
  add_dependencies(${target} ${target}_shaders)
endfunction()
