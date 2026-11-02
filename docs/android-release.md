# Releasing an Android game on Google Play

How a game built on ZARO goes from a scene to a Google Play upload, and how the game's own
repository keeps the upload key safe. The requirements behind these rules, with sources and the
date they were checked, are in [DECISIONS.md](DECISIONS.md) D-025. The toolchain and the export
are in D-022.

## What ZARO does and does not do

- ZARO builds the App Bundle (AAB) and its native debug symbols file. It never holds a game's keys
  and never produces store-signed artifacts.
- Google signs what players install (Play App Signing). The game signs its uploads with an
  **upload key** that the game's owner keeps. A lost or leaked upload key can be reset in Play
  Console; the app signing key never leaves Google.
- Builds without an upload key, such as local test builds and ZARO's own CI, are signed with the
  Android debug key. Google Play rejects those.

## Building a release bundle

A release goes through the game's build pipeline (D-027). The game has a build file with its
version, its package name and a release environment (format at the top of
`src/scene/build_spec.h`), for example:

```toml
[game]
name = "mygame"
scene = "scene.toml"
version = "1.4.0"
package = "com.example.mygame"   # fixed at the first upload
title = "My Game"                # the name under the icon
orientation = "portrait"         # or "landscape" (the default)

[environment.production]
release = true
```

```
python tools/android.py setup                      # once: pinned NDK, SDK, JDK, Gradle, bundletool
python tools/android.py aab game/build.toml --env production --release --version-code 12
```

This runs `zaro build game/build.toml --target android --release`, which checks everything below
before it builds anything. `--release` needs a release environment (`release = true`, which can
never enable debug tools), a version code and `package` in `[game]`. It reads the upload key from
the environment only, and stops with an error (ZE1003) naming each missing variable:

| Variable | Value |
|---|---|
| `ZARO_UPLOAD_KEYSTORE` | path to the keystore file (JKS or PKCS12) |
| `ZARO_UPLOAD_KEYSTORE_PASSWORD` | the keystore password |
| `ZARO_UPLOAD_KEY_ALIAS` | the alias of the upload key in it |
| `ZARO_UPLOAD_KEY_PASSWORD` | the key's password |

The version code comes from `--version-code` or `version_code` in `[game]`. Every upload needs a
higher one, up to 2100000000. `version` in `[game]` is what players see.

Choose `package` before the first upload: Google Play fixes the package name (application id)
then, and the app can never be renamed. It is reverse-domain style, two or more parts of letters,
digits and `_`, each starting with a letter. A build without it is named `dev.zaro.<scene name>`,
which is fine for local test builds but refused for a release. `title` and `orientation` can
change with any update; without them the app is named after the scene and runs in landscape.
`zaro build <build.toml> --target android --plan` shows all three under the export stage. The
rules and their reasons are D-037.

The pipeline checks the result before it reports success:

- `bundletool validate` passes.
- The bundle requests 16 KB zip alignment for native libraries (`PAGE_ALIGNMENT_16K`).
- The device APKs built from it pass `zipalign -c -P 16`.
- It is signed, and not with the debug key.

The native build has already failed if a library has a LOAD segment below 16 KB or links a
build-machine path (`cmake/ZaroCheckElf.cmake`). The output includes, under
`build/zaro-build/<game>-<environment>/android-project`:

```
symbols=.../native-debug-symbols.zip
aab=.../app/build/outputs/bundle/release/app-release.aab (12.0 MB)
signer=CN=... (SHA-256 ...)
```

`size_report.json` next to the symbols lists the native libraries and the largest assets.

Upload the AAB, and upload `native-debug-symbols.zip` for the same version in Play Console
(App bundle explorer, Downloads, Assets) so native crashes show function names and lines.

To try a bundle on a phone before uploading, use `python tools/android.py install-aab`. It builds
the APKs for the connected device with bundletool and installs them, signed with the local debug
key.

## Keeping the upload key in a game's CI

The game lives in its own repository, usually private. Its release workflow should hold the key
so that nobody can build a signed upload from a change that was not reviewed, and so that every
upload can be traced to the commit and workflow run that built it. On GitHub:

1. **Protect the branch.** Release builds run only from `main` (or release tags), which a ruleset
   protects: pull requests with review, no force pushes, no deletion.
2. **Put the secrets behind an environment.** Create an environment, for example `play-release`,
   and set:
   - Required reviewers: the owner. Each release job waits for an approval.
   - Deployment branches and tags: only `main` (or the release tag pattern).
   - Environment secrets: the keystore as base64, its password, the alias and the key password.

   Secrets of an environment are given only to jobs that name it and pass its rules. Workflows
   triggered from forks get no secrets at all.
3. **Give the job only what it needs.** `permissions: contents: read`, plus `id-token: write` and
   `attestations: write` for provenance. Do not run the release job on `pull_request`.
4. **Write the key to a temporary file and delete it.** Decode it into `$RUNNER_TEMP` for the
   build, point `ZARO_UPLOAD_KEYSTORE` at it, and remove it in a step that always runs. Do not
   print the variables or upload the file.
5. **Attest the outputs.** `actions/attest` (GitHub artifact attestations) records which
   repository, commit and workflow built the AAB and the symbols file. Anyone with access can
   check it later with `gh attestation verify app-release.aab -R OWNER/GAME`. Check that your
   GitHub plan offers attestations for private repositories.
6. **Upload the result,** by hand in Play Console or from the same job with the Google Play
   Developer API. A service account key for that API is another environment secret with the same
   rules.

A release job in that repository then looks like this (pin the actions by commit SHA, as ZARO's
own CI does):

```yaml
jobs:
  play-release:
    runs-on: ubuntu-24.04
    environment: play-release            # required reviewers + main only
    permissions:
      contents: read
      id-token: write
      attestations: write
    steps:
      - uses: actions/checkout@<sha>     # the game, with the ZARO version it pins
      - run: python zaro/tools/android.py setup --accept-licenses
      - run: python zaro/tools/build.py release
      - name: Upload key from the environment's secrets
        env:
          KEYSTORE_B64: ${{ secrets.UPLOAD_KEYSTORE_B64 }}
        run: |
          umask 077
          echo "$KEYSTORE_B64" | base64 -d > "$RUNNER_TEMP/upload.jks"
      - name: Signed App Bundle
        env:
          ZARO_UPLOAD_KEYSTORE: ${{ runner.temp }}/upload.jks
          ZARO_UPLOAD_KEYSTORE_PASSWORD: ${{ secrets.UPLOAD_KEYSTORE_PASSWORD }}
          ZARO_UPLOAD_KEY_ALIAS: ${{ secrets.UPLOAD_KEY_ALIAS }}
          ZARO_UPLOAD_KEY_PASSWORD: ${{ secrets.UPLOAD_KEY_PASSWORD }}
        run: >
          python zaro/tools/android.py aab game/build.toml --env production --release
          --version-code ${{ github.run_number }}
      - if: always()
        run: rm -f "$RUNNER_TEMP/upload.jks"
      - uses: actions/attest@<sha>
        with:
          subject-path: |
            build/zaro-build/mygame-production/android-project/app/build/outputs/bundle/release/app-release.aab
            build/zaro-build/mygame-production/android-project/native-debug-symbols.zip
      - uses: actions/upload-artifact@<sha>
        with:
          name: play-release
          path: |
            build/zaro-build/mygame-production/android-project/app/build/outputs/bundle/release/app-release.aab
            build/zaro-build/mygame-production/android-project/native-debug-symbols.zip
```

ZARO's own CI runs the same `aab --release` path (`examples/crowd/build.toml`, environment
`production`) with a throwaway key it generates for each run,
so the signing path is tested without any real key.
