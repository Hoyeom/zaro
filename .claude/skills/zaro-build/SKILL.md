---
name: zaro-build
description: The game build pipeline (D-027) — zaro build and its stages (validate, pre, native, export, post, package, sign), game build files with versions, environments and config, pre and post steps from the game and from plugins' build extensions, built-in steps (check, test, design-check, cook-svg) and how to add one, build_info.toml stamping, the app's package name, title and orientation, the release rule against debug tools, and the ZE1000-ZE1003 errors. Load before writing or changing a build.toml, a [[step]], a built-in step in src/app/pipeline.cpp, or anything a target's build stages run.
---

# The build pipeline (D-027)

`zaro build <build.toml> [--env NAME] [--version-code N] [--out DIR] [--plan] [--target android
[--package aab|apk] [--release]]`. The file format is
at the top of `src/scene/build_spec.h`; the stages and the runner are described at the top of
`src/app/pipeline.h`. `examples/crowd/build.toml` is a small example.

- **Stages** run in a fixed order: validate, pre, native, export, post, package, sign. Without a
  target only validate and pre run, and the post steps wait. `--target android` adds the others:
  - native: `tools/build.py android`
  - export: `zaro export android`, with `build_info.toml` packed and the build file left out
  - post: the build extensions, then the post steps, then `android.py stage-post` (symbols, size
    report)
  - package: `stage-package` (Gradle and bundletool checks; `--package aab|apk`)
  - sign: `stage-sign`

  The tools come from the checkout zaro was built from (`ZARO_SOURCE_DIR`, `ZARO_PYTHON` in
  CMakeLists.txt). Usually run as `tools/android.py aab|apk <build.toml>` (zaro-android).
- **App identity** (D-037): `[game] package`, `title` and `orientation` (`portrait` or
  `landscape`) go to the export as `--package`, `--title` and `--portrait`; `--plan` prints them
  under export. Unset keys keep the export's defaults (`dev.zaro.<scene name>`, the scene's name,
  landscape). The rules are `scene::valid_package_name` and `scene::valid_app_title` (ZE1000).
- **Release**: `--release` needs `--target`, a release environment, a version code, `[game]
  package` and the four `ZARO_UPLOAD_*` variables. validate reports every missing one (ZE1003)
  before anything builds.
- **Validate first**: `--plan` loads the build file, runs `zaro check` on the scene, loads every
  build extension and checks each step, then prints the plan and runs nothing. Use it after any
  edit.
- **Steps**:
  - Each step has a `stage` of `pre` or `post`, a unique `name`, and either `run = [program,
    args...]` or `uses = "<built-in>"` with `with = [args...]`.
  - Plugins declare steps in their build extension (`[[step]]`, D-026). They run before the game's
    steps of the same stage.
  - A step runs in the directory of the file that declares it, and the arguments of built-in steps
    are paths relative to that file. Arguments may name `{out}`, `{env}`, `{version}` and
    `{version_code}`.
- **No hidden scripts**: `run` starts the program without a shell. Shells and inline code (`-c`,
  `-e`) are refused (ZE1000). To run logic, ship it as a file in the game or plugin repository
  and run that file.
- **Built-in steps** are the table `kBuiltinSteps` in `src/app/pipeline.cpp`: `check`, `test`,
  `design-check` and `cook-svg` (cooks the SVG sprite sheets of the `with` scenes or sheets, or of
  the game's scene, by content hash; `zaro-scene`, D-028). A new one is one more entry: a name, a
  usage string and a function that gets the resolved `with` arguments and returns 0 or prints its
  own errors.
  `zaro build --plan` rejects unknown names with the list of known ones.
- **Stamping**: before the pre steps, the pipeline writes `<out>/build_info.toml` (default
  `build/zaro-build/<game>-<env>`). It holds the game, version, version code, commit,
  environment, `release`, `debug_tools` and the environment's `config`.
- **Release cannot debug**: a `release = true` environment cannot set `debug_tools = true` or a
  config key starting with `debug` (ZE1002). Put debugging switches in `debug_tools`, never in
  `config`.
- **Errors**: ZE1000 for the build file, a step and unknown built-ins; ZE1001 for a failed step
  (with the step's file, line and exit code); ZE1002 for the release rule.
- **Tests**: `tests/test_build_pipeline.py` (ctest `build_pipeline`) with data in
  `tests/data/pipeline/`. Extend it when the pipeline changes.
