# Android runtime package (D-021): everything `zaro export android` needs besides the scene.
#
#   <build>/android-runtime/
#     app/build.gradle, AndroidManifest.xml, ...   Gradle project template (src/platform/android/project)
#     app/src/main/java/org/libsdl/app/*.java      SDL's Java side, from the pinned SDL source
#     app/src/main/jniLibs/arm64-v8a/*.so          libmain.so (zaro), libSDL3.so, libwgpu_native.so, stripped
#     app/src/main/assets/shaders/*.wgsl           shaders compiled by the host's slangc
#     app/src/main/assets/fonts/                   the bundled fonts (D-019)
#
# The template holds @ZARO_...@ placeholders that the export fills in, so this directory is not a
# Gradle project by itself. Unstripped libraries stay in the build directory for ndk-stack.

set(_rt ${CMAKE_BINARY_DIR}/android-runtime)
set(_jni ${_rt}/app/src/main/jniLibs/${ANDROID_ABI})
set(_template ${CMAKE_SOURCE_DIR}/src/platform/android/project)
file(GLOB_RECURSE _template_files CONFIGURE_DEPENDS ${_template}/*)

add_custom_target(zaro_android_runtime ALL
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${_rt}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${_template} ${_rt}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${sdl3_SOURCE_DIR}/android-project/app/src/main/java/org/libsdl/app
          ${_rt}/app/src/main/java/org/libsdl/app
  COMMAND ${CMAKE_COMMAND} -E make_directory ${_jni}
  COMMAND ${CMAKE_STRIP} --strip-unneeded -o ${_jni}/libmain.so $<TARGET_FILE:zaro>
  COMMAND ${CMAKE_STRIP} --strip-unneeded -o ${_jni}/libSDL3.so $<TARGET_FILE:SDL3::SDL3-shared>
  COMMAND ${CMAKE_STRIP} --strip-unneeded -o ${_jni}/libwgpu_native.so $<TARGET_FILE:zaro_wgpu>
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/shaders ${_rt}/app/src/main/assets/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/fonts ${_rt}/app/src/main/assets/fonts
  DEPENDS zaro ${_template_files}
  COMMENT "android runtime package: ${_rt}"
  VERBATIM)
add_dependencies(zaro_android_runtime zaro_shaders)
