#!/usr/bin/env python3
"""ZARO branch, commit and PR-title rules (D-015).

Usage:
  python tools/git_rules.py commit-msg FILE   check a commit message (git commit-msg hook)
  python tools/git_rules.py branch            check the current branch (git pre-commit hook)
  python tools/git_rules.py pre-push          check refs on stdin (git pre-push hook)
  python tools/git_rules.py commits RANGE      check every commit in RANGE (CI, e.g. origin/main..HEAD)
  python tools/git_rules.py pr-title TITLE    check a PR title (becomes the squash commit)
  python tools/git_rules.py --list-rules

Exit codes: 0 ok, 1 violation.
"""

from __future__ import annotations

import re
import subprocess
import sys

TYPES = ("feat", "fix", "perf", "refactor", "docs", "test", "build", "ci", "chore", "revert")
# Engine modules (src/<module>) plus repository areas.
SCOPES = ("core", "platform", "rhi", "sim", "render", "app", "cli", "abi", "shaders",
          "tools", "cmake", "deps", "docs", "ci", "repo")
PROTECTED = ("main",)
MAX_HEADER = 72

HEADER_RE = re.compile(r"^(?P<type>[a-z]+)(\((?P<scope>[a-z0-9-]+)\))?(?P<bang>!)?: (?P<subject>.+)$")
BRANCH_RE = re.compile(r"^(?P<type>[a-z]+)/[a-z0-9][a-z0-9.-]*$")

RULES = {
    "G001": f"header must be 'type(scope): subject'; type in {', '.join(TYPES)}",
    "G002": f"scope, when given, must be one of {', '.join(SCOPES)}",
    "G003": f"header at most {MAX_HEADER} characters, subject not ending with '.'",
    "G004": "commit needs a DCO 'Signed-off-by:' line matching the author (git commit -s)",
    "G005": "no commits or pushes directly to main; work on a 'type/short-name' branch",
    "G006": "branch name must be 'type/short-name' (lowercase, digits, '-', '.')",
}


def err(where: str, rule: str, msg: str) -> None:
    print(f"{where}: error {rule}: {msg}", file=sys.stderr)


def check_header(header: str, where: str) -> int:
    m = HEADER_RE.match(header)
    if not m or m.group("type") not in TYPES:
        err(where, "G001", f"'{header}' is not 'type(scope): subject' with type in {', '.join(TYPES)}"
            " (e.g. 'feat(sim): add flow field')")
        return 1
    bad = 0
    scope = m.group("scope")
    if scope and scope not in SCOPES:
        err(where, "G002", f"unknown scope '{scope}'; use one of {', '.join(SCOPES)} or omit it")
        bad += 1
    if len(header) > MAX_HEADER or header.endswith("."):
        err(where, "G003", f"header is {len(header)} characters (max {MAX_HEADER}) or ends with '.'")
        bad += 1
    return bad


def git(*args: str) -> str:
    return subprocess.run(["git", *args], capture_output=True, text=True).stdout.strip()


def check_message(text: str, email: str, where: str) -> int:
    """Checks one commit message; email is the author's address in angle brackets."""
    lines = [l for l in text.splitlines() if not l.startswith("#")]
    while lines and not lines[0].strip():
        lines.pop(0)
    header = lines[0] if lines else ""
    # Messages git writes itself: local sync merges and autosquash markers.
    if header.startswith(("Merge ", "fixup! ", "squash! ", "amend! ")):
        return 0
    bad = check_header(header, f"{where}:1")
    signoffs = [l for l in lines if l.startswith("Signed-off-by: ")]
    if not signoffs or (email and not any(email in l for l in signoffs)):
        err(where, "G004", f"add 'Signed-off-by: <name> {email}' (commit with -s)")
        bad += 1
    return bad


def cmd_commit_msg(path: str) -> int:
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    ident = git("var", "GIT_AUTHOR_IDENT")
    email = ident[ident.find("<"):ident.find(">") + 1] if "<" in ident else ""
    return 1 if check_message(text, email, path) else 0


def cmd_commits(rev_range: str) -> int:
    """CI: checks every commit in a range such as origin/main..HEAD."""
    bad = 0
    for sha in git("rev-list", "--no-merges", rev_range).split():
        text = git("log", "-1", "--format=%B", sha)
        email = "<" + git("log", "-1", "--format=%ae", sha) + ">"
        bad += check_message(text, email, f"commit {sha[:10]}")
    return 1 if bad else 0


def cmd_branch() -> int:
    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    if branch in PROTECTED:
        err("branch", "G005", f"committing on '{branch}'; create a branch: git switch -c feat/<name>")
        return 1
    if branch == "HEAD":  # detached (rebase in progress); nothing to check
        return 0
    m = BRANCH_RE.match(branch)
    if not m or m.group("type") not in TYPES:
        err("branch", "G006", f"'{branch}' should be 'type/short-name' with type in {', '.join(TYPES)}")
        return 1
    return 0


def cmd_pre_push() -> int:
    bad = 0
    for line in sys.stdin:
        parts = line.split()
        if len(parts) == 4 and parts[2] in (f"refs/heads/{p}" for p in PROTECTED):
            err("push", "G005", f"pushing to {parts[2]}; open a pull request instead")
            bad += 1
    return 1 if bad else 0


def main(argv: list[str]) -> int:
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    if argv[0] == "--list-rules":
        for k, v in RULES.items():
            print(f"{k}  {v}")
        return 0
    if argv[0] == "commit-msg" and len(argv) == 2:
        return cmd_commit_msg(argv[1])
    if argv[0] == "commits" and len(argv) == 2:
        return cmd_commits(argv[1])
    if argv[0] == "branch":
        return cmd_branch()
    if argv[0] == "pre-push":
        return cmd_pre_push()
    if argv[0] == "pr-title" and len(argv) == 2:
        return 1 if check_header(argv[1], "pr-title") else 0
    print(__doc__, file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
