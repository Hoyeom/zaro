#!/usr/bin/env python3
"""Configure, build and optionally test ZARO with one command.

Usage:
  python tools/build.py [preset] [--test] [--no-gpu-tests] [--clean] [--engine-budget=SECONDS]

preset is one of the configure presets in CMakePresets.json (default: debug). `android`
cross-compiles libmain.so with the NDK that `python tools/android.py setup` installs and writes
the runtime package to build/android/android-runtime (cmake/ZaroAndroid.cmake).
--no-gpu-tests skips ctest tests labelled `gpu` (hosted CI runners have no GPU).
--engine-budget=S deletes ZARO's own object files after the build, rebuilds them alone and
fails if that takes longer than S seconds (`engine_build_seconds=<n>`). Third-party code is
excluded, so the number tracks the engine's own compile time (D-001, D-013).

On Windows this locates the MSVC toolchain (vcvars64.bat) and the CMake/Ninja
that ship with Visual Studio, so nothing has to be on PATH beforehand.
Prints the wall-clock build time on the last line as `build_seconds=<n>`.
"""

from __future__ import annotations

import glob
import locale
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# The second alternative is the Korean-localized prefix, escaped to keep the source ASCII.
SHOW_INCLUDES = re.compile("^(Note: including file:|\uCC38\uACE0: \uD3EC\uD568 \uD30C\uC77C:)")
# Windows: run the toolchain without a console. cl.exe encodes /showIncludes notes in the
# console's current code page, and some build steps switch that code page while compiles
# run in parallel; Ninja then misses the localized prefix and drops header dependencies.
DETACHED_PROCESS = 0x00000008

VS_ROOTS = [
    r"C:\Program Files\Microsoft Visual Studio",
    r"C:\Program Files (x86)\Microsoft Visual Studio",
]


def find_vs_install() -> Path | None:
    vswhere = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
    if vswhere.exists():
        out = subprocess.run(
            [str(vswhere), "-latest", "-products", "*", "-prerelease",
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"],
            capture_output=True, text=True,
        ).stdout.strip()
        if out:
            return Path(out.splitlines()[0])
    # vswhere can miss Build Tools installs whose instance registration is stale.
    hits = []
    for root in VS_ROOTS:
        hits += glob.glob(os.path.join(root, "*", "*", "VC", "Auxiliary", "Build", "vcvars64.bat"))
    if not hits:
        return None
    return Path(sorted(hits)[-1]).parents[3]


def msvc_env(vs: Path) -> dict[str, str]:
    vcvars = vs / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    out = subprocess.run(f'cmd /s /c ""{vcvars}" >nul && set"',
                         capture_output=True, text=True, shell=True, check=True).stdout
    env = {}
    for line in out.splitlines():
        k, sep, v = line.partition("=")
        if sep:
            env[k] = v
    # Prefer the CMake/Ninja bundled with Visual Studio when none is on PATH.
    bundled = vs / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake"
    extra = [str(bundled / "CMake" / "bin"), str(bundled / "Ninja")]
    env["PATH"] = os.pathsep.join([env.get("PATH", "")] + extra)
    return env


def repair_header_deps(env: dict[str, str], build_dir: Path) -> int:
    """Deletes ZARO objects whose header dependencies Ninja failed to record.

    With a Korean-localized MSVC, parallel clean builds leave some objects with no recorded
    /showIncludes dependencies, so later header edits would not rebuild them. Rebuilding those
    objects alone records the dependencies. Third-party objects (under _deps/) are left alone.
    Returns the number of objects deleted.
    """
    ninja = shutil.which("ninja", path=env.get("PATH"))
    if not ninja or not (build_dir / ".ninja_deps").exists():
        return 0
    out = subprocess.run([ninja, "-C", str(build_dir), "-t", "deps"], capture_output=True,
                         env=env, creationflags=DETACHED_PROCESS).stdout.decode("utf-8", "replace")
    stale = [line.split(":")[0] for line in out.splitlines()
             if line.startswith("CMakeFiles/") and "#deps 0," in line]
    for obj in stale:
        (build_dir / obj).unlink(missing_ok=True)
    if stale:
        print(f"build.py: rebuilding {len(stale)} object(s) whose header deps were not recorded")
    return len(stale)


def remove_engine_objects(build_dir: Path) -> int:
    """Deletes object files of ZARO-owned targets (CMakeFiles/zaro*.dir, test_*.dir)."""
    count = 0
    for pattern in ("zaro*.dir", "test_*.dir"):
        for target_dir in (build_dir / "CMakeFiles").glob(pattern):
            for obj in list(target_dir.rglob("*.obj")) + list(target_dir.rglob("*.o")):
                obj.unlink()
                count += 1
    return count


def main(argv: list[str]) -> int:
    preset = next((a for a in argv if not a.startswith("--")), "debug")
    budget = None
    for a in argv:
        if a.startswith("--engine-budget="):
            budget = float(a.split("=", 1)[1])
    # Tool output can hold characters the console code page cannot encode; never crash on them.
    sys.stdout.reconfigure(errors="replace")
    env = dict(os.environ)
    if sys.platform == "win32" and not shutil.which("cl"):
        vs = find_vs_install()
        if vs is None:
            print("build.py: error: no MSVC toolchain found; install Visual Studio 2022 "
                  "Build Tools with the 'Desktop development with C++' workload", file=sys.stderr)
            return 1
        env = msvc_env(vs)
    if preset == "android":
        # Cross build with the pinned NDK (tools/android.py, D-021). Tests run on the host only.
        sys.path.insert(0, str(ROOT / "tools"))
        import android
        if not (android.ndk_dir() / "build" / "cmake" / "android.toolchain.cmake").exists():
            print(f"build.py: error: NDK {android.NDK_VERSION} not found at {android.ndk_dir()}; "
                  "run `python tools/android.py setup`", file=sys.stderr)
            return 1
        if "--test" in argv:
            print("build.py: error: the android preset has no tests; tests run on the host",
                  file=sys.stderr)
            return 1
        env["ANDROID_NDK_HOME"] = str(android.ndk_dir())

    def run(*cmd: str) -> None:
        print("+", " ".join(cmd), flush=True)
        # On Windows CreateProcess searches the parent's PATH, not env's; resolve explicitly.
        exe = shutil.which(cmd[0], path=env.get("PATH")) or cmd[0]
        flags = DETACHED_PROCESS if sys.platform == "win32" else 0
        proc = subprocess.Popen([exe, *cmd[1:]], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, creationflags=flags)
        assert proc.stdout is not None
        for raw in proc.stdout:
            try:
                line = raw.decode("utf-8")
            except UnicodeDecodeError:
                line = raw.decode(locale.getpreferredencoding(False), errors="replace")
            if not SHOW_INCLUDES.match(line):
                sys.stdout.write(line)
                sys.stdout.flush()
        if proc.wait() != 0:
            raise subprocess.CalledProcessError(proc.returncode, cmd)

    if "--clean" in argv:
        shutil.rmtree(ROOT / "build" / preset, ignore_errors=True)

    start = time.perf_counter()
    try:
        run("cmake", "--preset", preset)
        run("cmake", "--build", "--preset", preset)
        if sys.platform == "win32" and repair_header_deps(env, ROOT / "build" / preset):
            run("cmake", "--build", "--preset", preset)
        elapsed = time.perf_counter() - start
        engine_seconds = None
        if budget is not None:
            removed = remove_engine_objects(ROOT / "build" / preset)
            t0 = time.perf_counter()
            run("cmake", "--build", "--preset", preset)
            engine_seconds = time.perf_counter() - t0
            print(f"engine_build_seconds={engine_seconds:.1f} objects={removed} budget={budget:g}")
        if "--test" in argv:
            ctest = ["ctest", "--preset", preset]
            if "--no-gpu-tests" in argv:
                ctest += ["-LE", "gpu"]
            run(*ctest)
    except subprocess.CalledProcessError as e:
        return e.returncode or 1
    print(f"build_seconds={elapsed:.1f}")
    if engine_seconds is not None and engine_seconds > budget:
        print(f"build.py: error: engine rebuild took {engine_seconds:.1f}s, budget is {budget:g}s",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
