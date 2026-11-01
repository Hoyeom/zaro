---
name: zaro-android
description: Building and running ZARO on Android (D-022) — the pinned toolchain from tools/android.py, the android preset and libmain.so, zaro export android and what it packs, launching with arguments and pulling perf.json over adb, touch input and the background/foreground surface, and the APK CI job. Load before touching tools/android.py, src/platform/android/, the android preset or the export, and when an APK does not build, install or start.
---

# Android (D-022)

Commands are in the CLAUDE.md "Commands" table (`tools/android.py setup|doctor|apk|run|perf`,
`tools/build.py android`, `zaro export android`).

- `python tools/android.py setup` installs the pinned NDK, SDK packages, JDK and Gradle (versions
  at the top of `tools/android.py`). It accepts the Android SDK license only with
  `--accept-licenses`; otherwise it shows the license.
- The `android` preset builds `libmain.so`: the `zaro` command line as a shared library that
  `dev.zaro.ZaroActivity` (an `SDLActivity`) calls. It writes `build/android/android-runtime`, a
  Gradle template with the libraries and shaders. The template lives in
  `src/platform/android/project`; `@ZARO_...@` placeholders are filled in by the export.
- `zaro export android <scene>` copies the scene's directory (without `tests/` and `*.test.toml`)
  and every file the scene names outside it (script, prefabs, screens with their prefabs and
  images, string tables) to `assets/game/`. On the device they are unpacked to
  `/sdcard/Android/data/<package>/files`, which becomes the working directory. Output goes to
  logcat: `adb logcat -s zaro`.
- Launch with other arguments: `adb shell am start -S -n <package>/dev.zaro.ZaroActivity --es args
  "'run game/scene.toml --headless --perf perf.json'"`. `tools/android.py perf` does this and pulls
  the report.
- Touch is a virtual stick in `platform::poll_input`. In the background the app drops its GPU
  surface (`rhi::set_window(device, nullptr)`) and recreates it when `Input::window_changed` is set.
- `SDL3/`, `android/` and `jni.h` belong to `platform` (lint Z007).
- Export errors are ZE0800 (no runtime package, or a scene file outside the packed tree). The
  build fails if `libmain.so` depends on a build-machine path (`cmake/ZaroCheckNeeded.cmake`).
- CI builds the APK (`android apk` job); running it needs a device and stays local.
