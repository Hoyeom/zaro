"""`zaro export android` without an Android build (D-022, D-025, D-026).

Builds a stand-in runtime package from the Gradle template (an empty libmain.so is enough for the
export), then checks the exported project: version, build extension dependencies, manifest
entries and sources, and the file:line errors for broken extensions and clashing sources.

Usage: python tests/test_android_export.py <zaro binary>   (run from the repository root)
"""

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = Path("tests/data/build_extensions")


def zaro(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(Path(sys.argv[1]).resolve()), *args], cwd=ROOT, capture_output=True, text=True)


def check(condition: bool, what: str, detail: str = "") -> None:
    if not condition:
        sys.exit(f"test_android_export: FAILED: {what}\n{detail}")


def main() -> int:
    tmp = Path(tempfile.mkdtemp(prefix="zaro-export-"))
    try:
        runtime = tmp / "runtime"
        shutil.copytree(ROOT / "src/platform/android/project", runtime)
        (runtime / "app/src/main/jniLibs/arm64-v8a").mkdir(parents=True)
        (runtime / "app/src/main/jniLibs/arm64-v8a/libmain.so").write_bytes(b"")
        out = tmp / "project"

        r = zaro("export", "android", str(DATA / "scene.toml"), "--runtime", str(runtime), "--out", str(out),
                 "--version-code", "7", "--version-name", "2.1.0")
        check(r.returncode == 0, "export with a build extension", r.stdout + r.stderr)
        gradle = (out / "app/build.gradle").read_text()
        manifest = (out / "app/src/main/AndroidManifest.xml").read_text()
        check("versionCode = 7" in gradle and "versionName = '2.1.0'" in gradle, "version from the options", gradle)
        check("    implementation 'com.example:library:1.2.3'" in gradle, "extension dependency", gradle)
        check('<uses-permission android:name="android.permission.VIBRATE" />' in manifest, "permission", manifest)
        check('<package android:name="com.example.store" />' in manifest, "manifest fragment", manifest)
        app = manifest[manifest.index("<application"):manifest.index("</application>")]
        check('android:name="com.example.key"' in app, "application fragment inside <application>", manifest)
        check("@ZARO_" not in gradle + manifest, "every placeholder filled", gradle + manifest)
        check((out / "app/src/main/java/com/example/ext/Hello.java").is_file(), "extension sources copied")
        assets = (out / "app/src/main/assets/zaro_assets.txt").read_text().split()
        check("game/scene.toml" in assets, "scene in the asset list", str(assets))
        check("game/plugin/build_extension.toml" not in assets and not any(a.endswith(".java") for a in assets),
              "the scene's build extensions and sources are not game assets", str(assets))

        # Exporting again into the same project replaces its own files without a clash.
        r = zaro("export", "android", str(DATA / "scene.toml"), "--runtime", str(runtime), "--out", str(out))
        check(r.returncode == 0, "export again into the same project", r.stdout + r.stderr)

        r = zaro("check", str(DATA / "bad_scene.toml"))
        text = r.stdout + r.stderr
        check(r.returncode == 1, "zaro check fails on a broken extension", text)
        for pattern in (r"bad_extension\.toml:5:\d+: error\[ZE0410\]: dependencies",
                        r"bad_extension\.toml:6:\d+: error\[ZE0410\]: permissions",
                        r"bad_extension\.toml:7:\d+: error\[ZE0410\]: application",
                        r"bad_extension\.toml:8:\d+: error\[ZE0406\]: .*did you mean 'sources'",
                        r"bad_scene\.toml:3:1: error\[ZE0406\]: cannot open build extension"):
            check(re.search(pattern, text) is not None, f"diagnostic {pattern}", text)

        r = zaro("export", "android", str(DATA / "clash_scene.toml"), "--runtime", str(runtime),
                 "--out", str(tmp / "clash"))
        text = r.stdout + r.stderr
        check(r.returncode == 1, "export refuses sources that replace the runtime's", text)
        check(re.search(r"clash[/\\]build_extension\.toml:5: .*ZaroActivity\.java", text) is not None,
              "clash reported at the extension's sources line", text)

        # Sounds (D-032): the sound file and its file sounds are packed; synthesized ones have no file.
        r = zaro("export", "android", str(ROOT / "tests/data/audio/scene.toml"), "--runtime", str(runtime),
                 "--out", str(tmp / "audio"))
        check(r.returncode == 0, "export a scene with sounds", r.stdout + r.stderr)
        assets = (tmp / "audio/app/src/main/assets/zaro_assets.txt").read_text().split()
        check("game/sounds.toml" in assets and "game/click.wav" in assets, "sound file and file sounds packed",
              str(assets))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_android_export: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
