---
name: zaro-git
description: The branch, commit and pull request rules of this repository and how a pull request lands — Conventional Commits with a module or area scope, DCO sign-off, squash merge, auto-merge once CI passes. Load before creating a branch, before writing a commit message, before opening or merging a pull request, when a git hook or the CI rules job rejects something, and when a stacked branch conflicts after its base was merged.
---

# Branches, commits and pull requests (D-015)

## Branches

- `main` always builds and passes tests. Nothing is committed or pushed to it directly.
- Work on a short-lived branch named `type/short-name`, e.g. `feat/flow-field-lod`,
  `fix/rhi-resolve-align`. Types: `feat fix perf refactor docs test build ci chore revert`.

## Commits

- Message: `type(scope): subject`, header at most 72 characters, no trailing period, plus a DCO
  sign-off (`git commit -s`) that matches the author.
- Scope is optional. When given it is a module (`core platform rhi sim scene script render app cli`)
  or an area (`abi shaders tools cmake deps docs ci repo`). The list is `SCOPES` in
  `tools/git_rules.py`; a new module adds its scope there.
  Example: `fix(rhi): align query resolve offset to 256 bytes`.
- The author email is the GitHub noreply address set in this clone's git config
  (`git config user.email`). A personal address never enters the history of this public repository.
- Run `python tools/build.py debug --test` before pushing.

## Pull requests

- One pull request per logical change. Changes are merged by **squash**; the PR title becomes the
  commit on `main`, so it follows the same `type(scope): subject` format. Reverting a PR is one
  `git revert`.
- **Land it without the owner watching.** Right after opening a PR, enable auto-merge:
  `gh pr merge <n> --squash --auto`. It merges once the required checks pass. Then go on with the
  next step instead of waiting; if CI fails, fix it on the same branch.
- A branch stacked on another PR's branch conflicts once that PR is squash-merged. Merge
  `origin/main` into the stacked branch and resolve; do not rebase a pushed branch.

## What enforces this

`tools/git_rules.py` (rules G001–G006, `--list-rules`) runs from the git hooks in `tools/githooks/`
(enable once per clone with `git config core.hooksPath tools/githooks`):

| Hook | Checks |
|---|---|
| `pre-commit` | branch name, no commit on `main`; also runs `tools/lint.py --staged` |
| `commit-msg` | message format and sign-off |
| `pre-push` | no push to `main` |

`permissions.deny` in `.claude/settings.json` blocks pushes to `main` and `--no-verify`.

CI (`.github/workflows/ci.yml`) runs on every pull request and on pushes to `main`:

1. `rules` (Ubuntu): `tools/lint.py`, then the PR title and every PR commit through
   `tools/git_rules.py pr-title` and `commits` (format and DCO sign-off).
2. `build` on Windows, Linux and macOS: `tools/build.py release --test --no-gpu-tests
   --engine-budget=180`, then a simulation-only benchmark (`zaro run --sim-only`). Its numbers go
   to the job summary and a `perf-<os>` artifact.

Hosted runners have no GPU, so `gpu`-labelled tests and GPU timings run only locally. Linux system
packages are listed in `tools/ci/install-linux-deps.sh`, which CI and `tools/ci/linux-docker.sh`
share. Actions are pinned by commit SHA.

## GitHub settings

The repository allows squash merges only, uses the PR title as the commit title, and deletes merged
branches. The `main` ruleset (`tools/github/ruleset-main.json`, applied on GitHub) blocks deletion,
force pushes and direct pushes. It also requires the four CI checks (`lint and repository rules`
and `build and test` on each OS) before a squash merge.

Editing the JSON file does not change GitHub. Re-apply it with
`gh api -X PUT repos/Hoyeom/zaro/rulesets/<id> --input tools/github/ruleset-main.json`; the ruleset
id is in `gh api repos/Hoyeom/zaro/rulesets`.
