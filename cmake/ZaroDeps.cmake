# Third-party dependencies, pinned by version and SHA-256 (D-004, D-012 licenses).
#
# Downloads are cached in .cache/deps so every preset shares them. Prebuilt binaries are used
# where the upstream project ships them (wgpu-native, Slang); the rest builds from source.

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.cache/deps" CACHE PATH "" FORCE)
set(FETCHCONTENT_QUIET OFF)

if(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
  set(_zaro_host windows-x86_64)
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  set(_zaro_host macos-aarch64)
elseif(UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
  set(_zaro_host linux-x86_64)
else()
  message(FATAL_ERROR "zaro: unsupported host ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
endif()

# --- wgpu-native v29.0.1.1 (MIT OR Apache-2.0), prebuilt -------------------------------------
set(_wgpu_base https://github.com/gfx-rs/wgpu-native/releases/download/v29.0.1.1)
if(_zaro_host STREQUAL windows-x86_64)
  set(_wgpu_url ${_wgpu_base}/wgpu-windows-x86_64-msvc-release.zip)
  set(_wgpu_sha 7e67d7445c42aeb85e30f88930fd8d7d83ee769e3390aeb1ada75ebf3cf78132)
elseif(_zaro_host STREQUAL macos-aarch64)
  set(_wgpu_url ${_wgpu_base}/wgpu-macos-aarch64-release.zip)
  set(_wgpu_sha a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e)
else()
  set(_wgpu_url ${_wgpu_base}/wgpu-linux-x86_64-release.zip)
  set(_wgpu_sha 95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a)
endif()
FetchContent_Declare(wgpu URL ${_wgpu_url} URL_HASH SHA256=${_wgpu_sha})
FetchContent_MakeAvailable(wgpu)

add_library(zaro_wgpu SHARED IMPORTED GLOBAL)
target_include_directories(zaro_wgpu INTERFACE ${wgpu_SOURCE_DIR}/include)
if(WIN32)
  set_target_properties(zaro_wgpu PROPERTIES
    IMPORTED_LOCATION ${wgpu_SOURCE_DIR}/lib/wgpu_native.dll
    IMPORTED_IMPLIB ${wgpu_SOURCE_DIR}/lib/wgpu_native.dll.lib)
elseif(APPLE)
  set_target_properties(zaro_wgpu PROPERTIES IMPORTED_LOCATION ${wgpu_SOURCE_DIR}/lib/libwgpu_native.dylib)
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

# --- SDL 3.4.18 (zlib), static, only the subsystems ZARO uses -----------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3 SYSTEM
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
  URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
FetchContent_MakeAvailable(SDL3)

# --- enkiTS v1.12 (zlib) -------------------------------------------------------------------------
set(ENKITS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_C_INTERFACE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(enkiTS SYSTEM
  URL https://github.com/dougbinks/enkiTS/archive/refs/tags/v1.12.tar.gz
  URL_HASH SHA256=8373b6199d56816bd3ba58432eae74e2ebd5afcfdffb723073cc34730b189fd5)
FetchContent_MakeAvailable(enkiTS)
target_include_directories(enkiTS SYSTEM INTERFACE ${enkits_SOURCE_DIR}/src)

# --- Tracy v0.14.1 (BSD-3), only with ZARO_TRACY ---------------------------------------------------
if(ZARO_TRACY)
  set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
  set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
  FetchContent_Declare(tracy SYSTEM
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
