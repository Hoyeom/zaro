#!/usr/bin/env python3
"""Android toolchain and APK workflow for ZARO (D-022).

Usage:
  python tools/android.py setup [--accept-licenses]   install the pinned toolchain
  python tools/android.py doctor                      print what is installed and where
  python tools/android.py apk <scene.toml> [--out DIR] [--package ID] [--release]
                                                      native build + `zaro export android` + Gradle
  python tools/android.py run [--out DIR] [-- zaro args...]
                                                      install the APK and start it (args optional)
  python tools/android.py perf [--out DIR] [--enemies N] [--frames N] [--warmup N] [--size WxH]
                                                      headless benchmark on the device, pulls perf.json

Toolchain versions are pinned below. `setup` installs them into the Android SDK directory
(ANDROID_HOME, default %LOCALAPPDATA%\\Android\\Sdk or ~/Android/Sdk) and a ZARO tools directory
(ZARO_TOOLCHAINS, default %LOCALAPPDATA%\\zaro or ~/.local/share/zaro). Every download is checked
against a pinned hash. Installing SDK packages needs the Android SDK license: `setup` shows it
interactively, or accepts it with --accept-licenses.

Commands that use a device need exactly one device in `adb devices` (or ANDROID_SERIAL set).
"""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# --- Pinned toolchain (D-022) ------------------------------------------------------------------
NDK_VERSION = "29.0.14206865"   # r29
PLATFORM = "android-36"         # compileSdk / targetSdk
BUILD_TOOLS = "36.0.0"
SDK_PACKAGES = ["platform-tools", f"platforms;{PLATFORM}", f"build-tools;{BUILD_TOOLS}",
                f"ndk;{NDK_VERSION}"]

# Google publishes SHA-1 for SDK archives (repository2-3.xml); sdkmanager checks the packages.
CMDLINE_TOOLS = {
    "win32": ("https://dl.google.com/android/repository/commandlinetools-win-16111833_latest.zip",
              "sha1", "57d04f2d75eb8e8fffc5000a987e5de4b5a63e9d"),
    "linux": ("https://dl.google.com/android/repository/commandlinetools-linux-16111833_latest.zip",
              "sha1", "e025545c62a8e64c7559119566a569fb1dec5f60"),
}
JDK_NAME = "jdk-21.0.13+9"      # Eclipse Temurin 21 (GPL-2.0 with Classpath Exception)
JDK = {
    "win32": ("https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/"
              "OpenJDK21U-jdk_x64_windows_hotspot_21.0.13_9.zip",
              "sha256", "a4dbaa05fca4bd89d84584e58c459bd732efebb1722af80ab13c6516832004f9"),
    "linux": ("https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/"
              "OpenJDK21U-jdk_x64_linux_hotspot_21.0.13_9.tar.gz",
              "sha256", "d7f5d5cf865a2902b70ff549b97586a7d3f234e016f041ab447ea740c7d66d1c"),
}
GRADLE_NAME = "gradle-9.8.2"    # Apache-2.0
GRADLE = ("https://services.gradle.org/distributions/gradle-9.8.2-bin.zip",
          "sha256", "21bd5da2de49b642904077f38c0efdc7778e92b3ed0889392dca61b97e389607")

ACTIVITY = "dev.zaro.ZaroActivity"
DEFAULT_OUT = ROOT / "build" / "android-project"
EXE = ".exe" if sys.platform == "win32" else ""
BAT = ".bat" if sys.platform == "win32" else ""


def host() -> str:
    if sys.platform == "win32":
        return "win32"
    if sys.platform.startswith("linux"):
        return "linux"
    sys.exit("android.py: error: setup supports Windows and Linux hosts; on macOS install the "
             f"SDK packages {SDK_PACKAGES}, JDK 17+ and {GRADLE_NAME} yourself and set ANDROID_HOME")


def sdk_root() -> Path:
    for var in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        if os.environ.get(var):
            return Path(os.environ[var])
    if sys.platform == "win32":
        return Path(os.environ["LOCALAPPDATA"]) / "Android" / "Sdk"
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Android" / "sdk"
    return Path.home() / "Android" / "Sdk"


def tools_root() -> Path:
    if os.environ.get("ZARO_TOOLCHAINS"):
        return Path(os.environ["ZARO_TOOLCHAINS"])
    if sys.platform == "win32":
        return Path(os.environ["LOCALAPPDATA"]) / "zaro"
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share")) / "zaro"


def ndk_dir() -> Path:
    return sdk_root() / "ndk" / NDK_VERSION


def java_home() -> Path | None:
    """JAVA_HOME if it is JDK 17 or newer, else the pinned JDK when installed."""
    env = os.environ.get("JAVA_HOME")
    if env and java_major(Path(env)) >= 17:
        return Path(env)
    pinned = tools_root() / JDK_NAME
    return pinned if (pinned / "bin" / f"java{EXE}").exists() else None


def java_major(home: Path) -> int:
    release = home / "release"
    if not release.exists():
        return 0
    for line in release.read_text(errors="replace").splitlines():
        if line.startswith("JAVA_VERSION="):
            v = line.split("=", 1)[1].strip('"')
            return int(v.split(".")[0]) if v.split(".")[0].isdigit() else 0
    return 0


def gradle_exe() -> Path:
    return tools_root() / GRADLE_NAME / "bin" / f"gradle{BAT}"


def adb_exe() -> Path:
    return sdk_root() / "platform-tools" / f"adb{EXE}"


def tool_env() -> dict[str, str]:
    env = dict(os.environ)
    env["ANDROID_HOME"] = str(sdk_root())
    env["ANDROID_NDK_HOME"] = str(ndk_dir())
    jh = java_home()
    if jh:
        env["JAVA_HOME"] = str(jh)
        env["PATH"] = os.pathsep.join([str(jh / "bin"), env.get("PATH", "")])
    return env


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    print("+", " ".join(str(c) for c in cmd), flush=True)
    kw.setdefault("env", tool_env())
    return subprocess.run([str(c) for c in cmd], check=True, **kw)


# --- setup -------------------------------------------------------------------------------------

def download(url: str, algo: str, digest: str, dest: Path) -> Path:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.exists() or hashlib.new(algo, dest.read_bytes()).hexdigest() != digest:
        print(f"android.py: downloading {url}", flush=True)
        tmp = dest.with_suffix(dest.suffix + ".part")
        with urllib.request.urlopen(url) as r, open(tmp, "wb") as f:
            shutil.copyfileobj(r, f, 1 << 20)
        got = hashlib.new(algo, tmp.read_bytes()).hexdigest()
        if got != digest:
            tmp.unlink()
            sys.exit(f"android.py: error: {url}: {algo} {got}, expected {digest}")
        tmp.replace(dest)
    return dest


def extract(archive: Path, into: Path) -> None:
    """Extracts into a fresh directory; keeps executable bits from zip files on POSIX."""
    shutil.rmtree(into, ignore_errors=True)
    into.mkdir(parents=True)
    if archive.name.endswith(".tar.gz"):
        with tarfile.open(archive) as t:
            t.extractall(into, filter="data")
        return
    with zipfile.ZipFile(archive) as z:
        base = os.path.abspath(into)
        for info in z.infolist():
            target = os.path.abspath(os.path.join(base, info.filename))
            if os.path.commonpath([base, target]) != base:
                sys.exit(f"android.py: error: {archive}: unsafe path {info.filename}")
            z.extract(info, into)
            mode = info.external_attr >> 16
            if mode and os.name == "posix":
                os.chmod(target, mode)


def install_archive(spec: tuple[str, str, str], name: str, dest: Path) -> None:
    """Downloads spec into the tools cache and moves its single top directory to dest."""
    url, algo, digest = spec
    archive = download(url, algo, digest, tools_root() / "downloads" / url.rsplit("/", 1)[1])
    staging = tools_root() / "staging" / name
    extract(archive, staging)
    tops = list(staging.iterdir())
    if len(tops) != 1:
        sys.exit(f"android.py: error: {archive}: expected one top-level directory")
    shutil.rmtree(dest, ignore_errors=True)
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(tops[0]), str(dest))
    shutil.rmtree(staging, ignore_errors=True)


def cmd_setup(args: list[str]) -> int:
    h = host()
    if java_home() is None:
        install_archive(JDK[h], "jdk", tools_root() / JDK_NAME)
    if not gradle_exe().exists():
        install_archive(GRADLE, "gradle", tools_root() / GRADLE_NAME)
    sdkmanager = sdk_root() / "cmdline-tools" / "latest" / "bin" / f"sdkmanager{BAT}"
    if not sdkmanager.exists():
        install_archive(CMDLINE_TOOLS[h], "cmdline-tools", sdk_root() / "cmdline-tools" / "latest")
    if "--accept-licenses" in args:
        run([sdkmanager, "--licenses"], input=b"y\n" * 64, stdout=subprocess.DEVNULL)
    else:
        run([sdkmanager, "--licenses"])
    # cmd.exe splits sdkmanager.bat arguments at ';', so the package names go through a file.
    package_file = tools_root() / "sdk-packages.txt"
    package_file.write_text("\n".join(SDK_PACKAGES) + "\n")
    run([sdkmanager, f"--package_file={package_file}"])
    return cmd_doctor([])


def cmd_doctor(_: list[str]) -> int:
    jh = java_home()
    rows = [
        ("Android SDK", sdk_root(), (sdk_root() / "platforms" / PLATFORM).exists()),
        (f"NDK {NDK_VERSION}", ndk_dir(), (ndk_dir() / "build" / "cmake" / "android.toolchain.cmake").exists()),
        (f"build-tools {BUILD_TOOLS}", sdk_root() / "build-tools" / BUILD_TOOLS,
         (sdk_root() / "build-tools" / BUILD_TOOLS).exists()),
        ("adb", adb_exe(), adb_exe().exists()),
        ("JDK 17+", jh or tools_root() / JDK_NAME, jh is not None),
        (GRADLE_NAME, gradle_exe(), gradle_exe().exists()),
    ]
    ok = True
    for name, path, found in rows:
        print(f"{'ok' if found else 'MISSING':8} {name:22} {path}")
        ok &= found
    if not ok:
        print("android.py: run `python tools/android.py setup` to install what is missing")
    return 0 if ok else 1


# --- build, export, device ----------------------------------------------------------------------

def host_zaro() -> Path:
    for preset in ("release", "debug"):
        exe = ROOT / "build" / preset / f"zaro{EXE}"
        if exe.exists():
            return exe
    sys.exit("android.py: error: no host zaro binary; run `python tools/build.py` first")


def option(args: list[str], name: str, default: str | None = None) -> str | None:
    if name in args:
        i = args.index(name)
        if i + 1 >= len(args):
            sys.exit(f"android.py: error: {name} needs a value")
        value = args[i + 1]
        del args[i:i + 2]
        return value
    return default


def cmd_apk(args: list[str]) -> int:
    out = Path(option(args, "--out", str(DEFAULT_OUT)))
    package = option(args, "--package")
    release = "--release" in args
    args = [a for a in args if a != "--release"]
    if len(args) != 1:
        sys.exit("android.py: error: apk needs one scene path")
    if cmd_doctor([]) != 0:
        return 1
    run([sys.executable, ROOT / "tools" / "build.py", "android"])
    export = [host_zaro(), "export", "android", args[0], "--out", out,
              "--runtime", ROOT / "build" / "android" / "android-runtime"]
    if package:
        export += ["--package", package]
    run(export)
    task = "assembleRelease" if release else "assembleDebug"
    run([gradle_exe(), "-p", out, task, "--console=plain"])
    apk = out / "app" / "build" / "outputs" / "apk" / ("release" if release else "debug")
    for f in sorted(apk.glob("*.apk")):
        print(f"apk={f}")
    return 0


def project_package(out: Path) -> str:
    gradle = (out / "app" / "build.gradle").read_text()
    for line in gradle.splitlines():
        line = line.strip()
        if line.startswith("applicationId"):
            return line.split("=", 1)[1].strip().strip("'\"")
    sys.exit(f"android.py: error: no applicationId in {out / 'app' / 'build.gradle'}")


def adb(*cmd: str, check: bool = True, capture: bool = False) -> str:
    print("+ adb", " ".join(cmd), flush=True)
    r = subprocess.run([str(adb_exe()), *cmd], check=check, text=True,
                       stdout=subprocess.PIPE if capture else None)
    return r.stdout or ""


def install_and_start(out: Path, zaro_args: list[str]) -> str:
    apks = sorted((out / "app" / "build" / "outputs" / "apk").rglob("*.apk"))
    if not apks:
        sys.exit(f"android.py: error: no APK under {out}; run `python tools/android.py apk <scene>`")
    package = project_package(out)
    adb("install", "-r", str(apks[-1]))
    adb("shell", "am", "force-stop", package)
    start = ["shell", "am", "start", "-W", "-n", f"{package}/{ACTIVITY}"]
    if zaro_args:
        # adb joins the words into one remote shell command; quote the extra as one string.
        start += ["--es", "args", "'" + " ".join(zaro_args) + "'"]
    adb(*start)
    return package


def cmd_run(args: list[str]) -> int:
    out = Path(option(args, "--out", str(DEFAULT_OUT)))
    zaro_args = args[args.index("--") + 1:] if "--" in args else []
    install_and_start(out, zaro_args)
    print("android.py: logs: adb logcat -s zaro")
    return 0


def cmd_perf(args: list[str]) -> int:
    out = Path(option(args, "--out", str(DEFAULT_OUT)))
    enemies = option(args, "--enemies", "20000")
    frames = option(args, "--frames", "1800")
    warmup = option(args, "--warmup", "300")
    size = option(args, "--size", "1920x1080")
    package = project_package(out)
    scene = (out / "app" / "src" / "main" / "assets" / "zaro_args.txt").read_text().split()[1]
    remote = f"/sdcard/Android/data/{package}/files/perf.json"
    adb("logcat", "-c", check=False)
    adb("shell", "rm", "-f", remote, check=False)
    install_and_start(out, ["run", scene, "--headless", "--enemies", enemies, "--warmup", warmup,
                            "--frames", frames, "--size", size, "--perf", "perf.json"])
    deadline = time.time() + 900
    while time.time() < deadline:
        time.sleep(3)
        if adb("shell", f"ls {remote} 2>/dev/null", check=False, capture=True).strip():
            break
        if not adb("shell", "pidof", package, check=False, capture=True).strip():
            adb("logcat", "-d", "-s", "zaro", "SDL", "AndroidRuntime", "DEBUG", check=False)
            sys.exit("android.py: error: the app ended without writing perf.json (log above)")
    else:
        sys.exit("android.py: error: no perf.json after 15 minutes; see `adb logcat -s zaro`")
    time.sleep(1)
    local = ROOT / "build" / "android-perf.json"
    adb("pull", remote, str(local))
    adb("logcat", "-d", "-s", "zaro", check=False)
    import json
    d = json.loads(local.read_text())
    ms = d["frame_ms"]
    print(f"perf: {d['enemies']} enemies, {d['fps']:.1f} fps, frame avg {ms['avg']:.2f} ms "
          f"p99 {ms['p99']:.2f} ms max {ms['max']:.2f} ms, sim {d['cpu_ms']['sim']:.2f} ms, "
          f"{d['adapter']} ({d['backend']}), {d['threads']} threads")
    # Roadmap P1 "Done when": at least 20,000 enemies at a stable 60 fps (p99 within one frame).
    ok = d["enemies"] >= 20000 and ms["p99"] <= 1000.0 / 60.0
    print(f"p1_target={'pass' if ok else 'fail'} (needs >= 20000 enemies and p99 <= 16.67 ms)")
    print(f"perf_json={local}")
    return 0


COMMANDS = {"setup": cmd_setup, "doctor": cmd_doctor, "apk": cmd_apk, "run": cmd_run, "perf": cmd_perf}


def main(argv: list[str]) -> int:
    if not argv or argv[0] not in COMMANDS:
        print(__doc__)
        return 0 if argv and argv[0] in ("-h", "--help", "help") else 2
    sys.stdout.reconfigure(errors="replace")
    try:
        return COMMANDS[argv[0]](argv[1:])
    except subprocess.CalledProcessError as e:
        return e.returncode or 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
