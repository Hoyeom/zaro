"""`zaro build` without a target (D-027): stages, step order, stamping and file:line errors.

Usage: python tests/test_build_pipeline.py <zaro binary>   (run from the repository root)
"""

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = "tests/data/pipeline"


def zaro(*args: str) -> subprocess.CompletedProcess:
    r = subprocess.run([str(Path(sys.argv[1]).resolve()), *args], cwd=ROOT, capture_output=True, text=True)
    r.text = r.stdout + r.stderr
    return r


def check(condition: bool, what: str, detail: str = "") -> None:
    if not condition:
        sys.exit(f"test_build_pipeline: FAILED: {what}\n{detail}")


def main() -> int:
    tmp = Path(tempfile.mkdtemp(prefix="zaro-build-"))
    try:
        r = zaro("build", f"{DATA}/game/build.toml", "--plan")
        check(r.returncode == 0, "plan", r.text)
        stages = [line for line in r.stdout.splitlines() if not line.startswith((" ", "zaro", "tests/"))]
        check(stages == ["validate", "pre", "native", "export", "post", "package", "sign"], "stage order", r.stdout)

        out = tmp / "production"
        r = zaro("build", f"{DATA}/game/build.toml", "--env", "production", "--version-code", "9",
                 "--out", str(out))
        check(r.returncode == 0, "build without a target", r.text)
        plugin, game = r.stdout.find("zaro build: plugin-step"), r.stdout.find("zaro build: game-step")
        check(0 <= plugin < game, "plugin steps run before the game's", r.stdout)
        check("game-post" not in r.stdout, "post steps wait for a target", r.stdout)
        info = (out / "build_info.toml").read_text()
        for line in ('game = "pipeline"', 'version = "3.2.1"', "version_code = 9", 'environment = "production"',
                     "release = true", "debug_tools = false", 'api = "https://api.example.com"'):
            check(line in info, f"build_info.toml has {line}", info)
        check(re.search(r'^commit = "[0-9a-f]{12}"$|^commit = "unknown"$', info, re.M) is not None, "commit", info)

        r = zaro("build", f"{DATA}/game/build.toml", "--env", "staging", "--out", str(tmp / "staging"))
        info = (tmp / "staging" / "build_info.toml").read_text()
        check(r.returncode == 0 and "debug_tools = true" in info and "retries = 3" in info and "fast = true" in info
              and "version_code = 4" in info, "staging environment and its config", r.text + info)

        r = zaro("build", f"{DATA}/game/build.toml", "--env", "nope")
        check(r.returncode == 2 and "ZE1000" in r.text and "staging, production" in r.text, "unknown environment",
              r.text)

        r = zaro("build", f"{DATA}/failing.toml", "--out", str(tmp / "failing"))
        check(r.returncode == 1 and re.search(r"error\[ZE1001\]: tests/data/pipeline/failing\.toml:8: step 'broken' "
                                              r"failed \(exit code 2\)", r.text) is not None, "failing step", r.text)

        r = zaro("build", f"{DATA}/unknown_builtin.toml", "--plan")
        check(r.returncode == 1 and re.search(r"unknown_builtin\.toml:8: step 'cook' uses 'cook-svg'", r.text)
              is not None, "unknown built-in step", r.text)

        r = zaro("build", f"{DATA}/bad.toml", "--plan")
        check(r.returncode == 1, "bad build file fails", r.text)
        for pattern in (r"bad\.toml:4:\d+: error\[ZE1000\]: unexpected key 'versoin' .*did you mean 'version'",
                        r"bad\.toml:8:\d+: error\[ZE1002\]: release environment 'production' cannot enable debug_tools",
                        r"bad\.toml:10:\d+: error\[ZE1002\]: .*cannot set 'debug_menu'",
                        r"bad\.toml:15:\d+: error\[ZE1000\]: step runs a shell",
                        r"bad\.toml:20:\d+: error\[ZE1000\]: step gives an interpreter inline code",
                        r"bad\.toml:23:\d+: error\[ZE1000\]: stage must be",
                        r"bad\.toml:22:\d+: error\[ZE1000\]: \[\[step\]\] needs either run"):
            check(re.search(pattern, r.text) is not None, f"diagnostic {pattern}", r.text)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_build_pipeline: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
