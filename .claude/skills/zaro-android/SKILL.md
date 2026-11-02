---
name: zaro-android
description: Building and running ZARO on Android (D-022, D-025) — the pinned toolchain from tools/android.py, the android preset and libmain.so, zaro export android and what it packs, Google Play release bundles and the upload-key boundary, launching with arguments and pulling perf.json over adb, touch input and the background/foreground surface, and the APK CI job. Load before touching tools/android.py, src/platform/android/, the android preset or the export, and when an APK does not build, install or start.
---

# Android (D-022)

Commands are in the CLAUDE.md "Commands" table (`tools/android.py setup|doctor|apk|aab|install-aab|run|perf`,
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
  images, sprite sheets with their PNGs, string tables) to `assets/game/`. SVG sheets are cooked
  first and pack their `.zaro-cache/<sheet>.png`, never the SVG files; the template's
  `ignoreAssetsPatterns` keeps AAPT from dropping that dot directory. On the device they are
  unpacked to `/sdcard/Android/data/<package>/files`, which becomes the working directory. Output
  goes to logcat: `adb logcat -s zaro`.
- Launch with other arguments: `adb shell am start -S -n <package>/dev.zaro.ZaroActivity --es args
  "'run game/scene.toml --headless --perf perf.json'"`. `tools/android.py perf` does this and pulls
  the report.
- Touch is a virtual stick in `platform::poll_input`. In the background the app drops its GPU
  surface (`rhi::set_window(device, nullptr)`) and recreates it when `Input::window_changed` is set.
- `SDL3/`, `android/` and `jni.h` belong to `platform` (lint Z007).
- Export errors are ZE0800 (no runtime package, or a scene file outside the packed tree).
- CI builds the APK (`android apk` job); running it needs a device and stays local.

## Google Play release builds (D-025)

- `python tools/android.py aab <build.toml> [--env NAME] [--release] [--version-code N]` runs
  `zaro build <build.toml> --target android --package aab` (D-027, zaro-build). That pipeline calls
  back `android.py stage-post`, `stage-package` and `stage-sign`:
  - post writes `native-debug-symbols.zip` from `build/android/android-symbols/<abi>/` (the
    unstripped libraries) and `size_report.json`.
  - package runs Gradle, `bundletool validate`, the `PAGE_ALIGNMENT_16K` check, and
    `zipalign -c -P 16` on the device APKs built from the bundle.
  - sign prints the signer and refuses a debug-signed release.
- The project is `build/zaro-build/<game>-<env>/android-project`. `apk <build.toml>` is the same
  pipeline with an APK, and `apk <scene.toml>` stays the quick path for a scene without a build
  file. `install-aab`, `run` and `perf` use the newest package (or `--out PROJECT`).
- **Signing boundary**: ZARO never holds a game's keys or produces store-signed artifacts. The
  upload key comes only from `ZARO_UPLOAD_KEYSTORE`, `ZARO_UPLOAD_KEYSTORE_PASSWORD`,
  `ZARO_UPLOAD_KEY_ALIAS` and `ZARO_UPLOAD_KEY_PASSWORD`. `zaro build --release` stops (ZE1003)
  when any is missing, when there is no version code or when the environment is not a release one; the Gradle template throws when only some are set. Without
  them, builds use the debug key. Never add a keystore, a password or a `signingConfig` with a
  literal path to the template or to a project.
- The version comes from the game: `zaro export android --version-code N --version-name S`
  (1..2100000000; the name may hold letters, digits, spaces and `._+-`). So do the package name,
  the label under the icon and the orientation (`--package`, `--title`, `--portrait`), which a
  build file sets in `[game]` (D-037). The manifest label is `@ZARO_TITLE@`; `@ZARO_NAME@` stays
  the Gradle project name.
- Every packed `.so` passes `cmake/ZaroCheckElf.cmake`: no NEEDED with a build-machine path, and
  every LOAD segment aligned to at least 16 KB.
- How a game's CI keeps the upload key (environment secrets behind required reviewers, `main`
  only, artifact attestations) is in `docs/android-release.md`. The requirements, with sources and
  the date they were checked, are D-025; re-check them and record the differences as a new
  D-number.
- The CI job builds a debug-key AAB and a release AAB with a key it generates for that run.

## Build extensions (D-026)

- A plugin's Android additions are a build extension file (format at the top of
  `src/scene/build_extension.h`). It holds Gradle dependencies, permissions, a `<manifest>` and an
  `<application>` XML fragment, and a directory of `.java`/`.kt` sources. A scene names it in
  `[scene] build_extensions = [...]`.
- `zaro check <scene>` validates the extensions (ZE0410 for a malformed dependency, permission or
  fragment, ZE0406 for unknown keys). `zaro export android` fills `@ZARO_GRADLE_DEPENDENCIES@`,
  `@ZARO_MANIFEST_EXTRA@` and `@ZARO_APPLICATION_EXTRA@` in the template and copies the sources.
  A source that would replace a runtime or another extension's file is ZE0800 at the extension's
  `sources` line.
- Never edit the template for one game or plugin; add a build extension instead.
- A build extension may also declare `[[step]]`s for `zaro build` (D-027, zaro-build).
- `tests/test_android_export.py` (ctest `android_export`) exports `tests/data/build_extensions/`
  against a stand-in runtime, so the export is tested without an Android build.
