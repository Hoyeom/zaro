#!/usr/bin/env python3
"""ZARO source rules checker.

Rules live here, not only in CLAUDE.md, so that they are enforced by the
Claude Code PostToolUse hook, the git pre-commit hook and CI alike.

Usage:
  python tools/lint.py                 lint every source file in the repo
  python tools/lint.py FILE...         lint the given files
  python tools/lint.py --staged        lint files staged in git (pre-commit)
  python tools/lint.py --hook          read a Claude Code hook payload on stdin
  python tools/lint.py --json          emit diagnostics as JSON
  python tools/lint.py --list-rules    print rule ids and descriptions

Suppress one finding on one line with a trailing comment:
  // zaro-lint: allow Z003 <reason>

Exit codes: 0 clean, 1 violations, 2 violations in --hook mode (blocks and
feeds stderr back to the agent).
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SOURCE_EXTS = {".c", ".cc", ".cpp", ".h", ".hpp", ".inl"}
HEADER_EXTS = {".h", ".hpp", ".inl"}
SCAN_DIRS = ("include", "src", "tests", "examples", "plugins")
SKIP_PARTS = {"third_party", "build", "out", ".git", ".cache"}

# Public headers (include/zaro/**) are the C ABI. They may include only these.
PUBLIC_HEADER_ALLOWED = {"stdint.h", "stddef.h", "stdbool.h"}

# Headers that cost seconds per translation unit or pull exceptions/RTTI.
BANNED_HEADERS = {
    "iostream", "istream", "ostream", "sstream", "fstream", "iomanip",
    "regex", "ranges", "format", "print", "any", "variant", "future",
    "functional", "filesystem", "locale", "codecvt",
}

MAX_INCLUDES_PER_HEADER = 12

# Module layering (CLAUDE.md "Modules"). A module under src/ may include only the engine
# modules listed for it. sim is the model and must stay free of platform and GPU code so it
# remains deterministic and testable headless; render is the view and never sees sim types.
MODULE_DEPS = {
    "core": {"core"},
    "platform": {"core", "platform"},
    "rhi": {"core", "platform", "rhi"},
    "sim": {"core", "sim"},
    "render": {"core", "rhi", "render"},
    "ui": {"core", "scene", "ui"},
    "scene": {"core", "scene"},
    "script": {"core", "scene", "script"},
    "app": {"core", "platform", "rhi", "sim", "render", "scene", "script", "ui", "app"},
    "cli": {"core", "app", "cli"},
}
# Third-party headers confined to the one module that wraps them.
THIRD_PARTY_OWNERS = {
    "SDL3/": "src/platform/",
    "android/": "src/platform/",
    "jni.h": "src/platform/",
    "webgpu/": "src/rhi/",
    "TaskScheduler.h": "src/core/jobs.cpp",
    "tracy/": "src/core/profile.h",
    "tomlc17.h": "src/scene/",
    "lua.h": "src/script/",
    "lualib.h": "src/script/",
    "luacode.h": "src/script/",
    "stb_truetype.h": "src/ui/font.cpp",
    "stb_image.h": "src/ui/image.cpp",
}

RULES = {
    "Z001": "public header (include/zaro/) includes something other than "
            + ", ".join(sorted(PUBLIC_HEADER_ALLOWED)) + " or another zaro/ header",
    "Z002": "banned heavy standard header (compile-time cost or exceptions)",
    "Z003": "exceptions/RTTI are disabled: no throw, try, catch, dynamic_cast, typeid",
    "Z004": f"header has more than {MAX_INCLUDES_PER_HEADER} #include lines",
    "Z005": "header is missing #pragma once",
    "Z006": "public header is missing an extern \"C\" guard",
    "Z007": "module layering: include of an engine module or third-party header this module may not use",
}

BANNED_KEYWORDS = re.compile(r"\b(throw|try|catch|dynamic_cast|typeid)\b")
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
# A suppression counts only when a reason follows the rule ids.
SUPPRESS_RE = re.compile(r"zaro-lint:\s*allow\s+((?:Z\d{3}[\s,]*)+)(\S.*)?")


@dataclass
class Diag:
    file: str
    line: int
    col: int
    rule: str
    message: str

    def text(self) -> str:
        return f"{self.file}:{self.line}:{self.col}: error {self.rule}: {self.message}"


def strip_comments_and_strings(src: str) -> list[str]:
    """Return lines with comments and string/char literals blanked out.

    Line count and column positions are preserved so diagnostics stay exact.
    """
    out = []
    i, n = 0, len(src)
    buf = []
    state = "code"
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "/":
                state = "line_comment"; buf.append("  "); i += 2; continue
            if c == "/" and nxt == "*":
                state = "block_comment"; buf.append("  "); i += 2; continue
            if c == '"':
                state = "string"; buf.append(" "); i += 1; continue
            if c == "'":
                state = "char"; buf.append(" "); i += 1; continue
            buf.append(c)
        elif state == "line_comment":
            if c == "\n":
                state = "code"; buf.append(c)
            else:
                buf.append(" ")
        elif state == "block_comment":
            if c == "*" and nxt == "/":
                state = "code"; buf.append("  "); i += 2; continue
            buf.append(c if c == "\n" else " ")
        elif state in ("string", "char"):
            quote = '"' if state == "string" else "'"
            if c == "\\":
                buf.append("  "); i += 2; continue
            if c == quote or c == "\n":
                state = "code"
            buf.append(c if c == "\n" else " ")
        i += 1
    out = "".join(buf).split("\n")
    return out


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def suppressed(raw_line: str, rule: str) -> bool:
    m = SUPPRESS_RE.search(raw_line)
    return bool(m) and rule in m.group(1) and bool(m.group(2))


def check_layering(rel_path: str, include: str) -> str | None:
    for prefix, owner in THIRD_PARTY_OWNERS.items():
        if include.startswith(prefix) and not rel_path.startswith(owner):
            return f"'{include}' may only be included from {owner}; go through that module's API"
    parts = rel_path.split("/")
    if len(parts) < 3 or parts[0] != "src":
        return None
    module = parts[1]
    allowed = MODULE_DEPS.get(module)
    target = include.split("/")[0]
    if allowed is None or "/" not in include or target not in MODULE_DEPS:
        return None
    if target not in allowed:
        return (f"module '{module}' may not include '{target}' (allowed: "
                f"{', '.join(sorted(allowed))}); see CLAUDE.md 'Modules'")
    return None


def lint_file(path: Path) -> list[Diag]:
    r = rel(path)
    parts = set(Path(r).parts)
    if parts & SKIP_PARTS or path.suffix not in SOURCE_EXTS:
        return []
    try:
        raw = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as e:
        return [Diag(r, 1, 1, "Z000", f"cannot read file: {e}")]

    raw_lines = raw.split("\n")
    code_lines = strip_comments_and_strings(raw)
    diags: list[Diag] = []
    is_header = path.suffix in HEADER_EXTS
    is_public = r.startswith("include/zaro/")

    def add(line_idx: int, col: int, rule: str, msg: str) -> None:
        if not suppressed(raw_lines[line_idx], rule):
            diags.append(Diag(r, line_idx + 1, col + 1, rule, msg))

    include_count = 0
    for idx, raw_line in enumerate(raw_lines):
        m = INCLUDE_RE.match(raw_line)
        if m:
            include_count += 1
            name = m.group(2).strip()
            base = name.split("/")[-1]
            if is_public and not (name in PUBLIC_HEADER_ALLOWED or name.startswith("zaro/")):
                add(idx, m.start(2), "Z001",
                    f"'{name}' is not allowed in a public C ABI header; "
                    "move it to the .cpp or a private header under src/")
            layer_msg = check_layering(r, name)
            if layer_msg:
                add(idx, m.start(2), "Z007", layer_msg)
            if base in BANNED_HEADERS and m.group(1) == "<":
                add(idx, m.start(2), "Z002",
                    f"<{name}> is banned (slow to compile or needs exceptions); "
                    "see CLAUDE.md 'C++ subset' for the replacement")

    for idx, code in enumerate(code_lines):
        if code.lstrip().startswith("#"):
            continue
        for km in BANNED_KEYWORDS.finditer(code):
            add(idx, km.start(), "Z003",
                f"'{km.group(1)}' is not allowed: the engine builds with exceptions "
                "and RTTI off; report errors by return value instead")

    if is_header:
        if include_count > MAX_INCLUDES_PER_HEADER:
            add(0, 0, "Z004",
                f"{include_count} includes (max {MAX_INCLUDES_PER_HEADER}); "
                "forward-declare or move includes into the .cpp")
        if not any(l.strip() == "#pragma once" for l in raw_lines):
            add(0, 0, "Z005", "add '#pragma once' at the top of the header")
        if is_public and 'extern "C"' not in raw:
            add(0, 0, "Z006",
                "wrap declarations in '#ifdef __cplusplus / extern \"C\" {' guards")

    return diags


def repo_files() -> list[Path]:
    files = []
    for d in SCAN_DIRS:
        base = ROOT / d
        if not base.is_dir():
            continue
        for p in base.rglob("*"):
            if p.is_file() and p.suffix in SOURCE_EXTS:
                files.append(p)
    return sorted(files)


def staged_files() -> list[Path]:
    out = subprocess.run(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"],
        cwd=ROOT, capture_output=True, text=True, check=True,
    ).stdout
    return [ROOT / l for l in out.splitlines() if Path(l).suffix in SOURCE_EXTS]


def hook_files() -> list[Path]:
    try:
        payload = json.load(sys.stdin)
    except json.JSONDecodeError:
        return []
    ti = payload.get("tool_input") or {}
    tr = payload.get("tool_response") or {}
    fp = ti.get("file_path") or ti.get("notebook_path") or tr.get("filePath")
    return [Path(fp)] if fp else []


def main(argv: list[str]) -> int:
    if "--list-rules" in argv:
        for k, v in RULES.items():
            print(f"{k}  {v}")
        return 0
    as_json = "--json" in argv
    hook = "--hook" in argv
    args = [a for a in argv if not a.startswith("--")]

    if hook:
        files = hook_files()
    elif "--staged" in argv:
        files = staged_files()
    elif args:
        files = [Path(a) for a in args]
    else:
        files = repo_files()

    diags = [d for f in files for d in lint_file(Path(f))]

    if as_json:
        print(json.dumps([asdict(d) for d in diags], indent=2))
    elif diags:
        stream = sys.stderr if hook else sys.stdout
        for d in diags:
            print(d.text(), file=stream)
        print(f"zaro-lint: {len(diags)} violation(s). Rules: python tools/lint.py --list-rules",
              file=stream)

    if diags:
        return 2 if hook else 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
