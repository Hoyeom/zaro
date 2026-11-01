---
name: zaro-docs
description: How documentation is written in this public repository — English only, one fact in one place, decisions as D-numbers, the roadmap kept current, rules backed by a check, and nothing about private repositories or the owner's backend platform. Load before writing or editing CLAUDE.md, a skill, the README, anything under docs/, a file-format comment at the top of a header, or a commit or PR body, and before recording a decision.
---

# Writing documentation here

## Everything here is public

The repository, its history and its pull requests are public (D-016). That covers docs, code
comments, commit messages and PR text.

- **English only.** Text a game shows to players (Korean UI strings in an example, a font test) is
  data, not documentation, and may be in any language.
- **No private repository appears here.** The owner's backend platform, its plugin and any
  proprietary SDK live in private repositories (D-012, D-016, D-018). When ZARO adopts a rule from
  them (UI navigation, the backend client's error and retry rules), restate the rule in ZARO's own
  terms. Do not copy their text, and do not name their types, files, packages or repositories. Refer
  to the source at most as "the backend platform", as docs/ROADMAP.md does.
- **No personal email address.** Commits use the noreply address (`zaro-git`).

## Where a fact goes

One fact lives in one place; other places link to it.

| What | Where |
|---|---|
| Commands, layout, modules, design principles, enforced rules, what not to do | `CLAUDE.md` |
| How to work in one area (git, docs, engine code, scenes) | a skill under `.claude/skills/` |
| A decision and its reasoning | `docs/DECISIONS.md`, as the next D-number |
| Phases, their scope and status, open decisions | `docs/ROADMAP.md` |
| A walkthrough for a new user | `docs/guide.md` |
| A file format or script API | the comment at the top of the header that owns it (`src/scene/scene.h`, `src/scene/test_spec.h`, `src/script/script.h`) |
| Status and a summary for visitors | `README.md` |

- **Decisions** are numbered `## D-0nn Title` and never renumbered. A decision that a later one
  replaces stays in place and gets a line saying which one supersedes it.
- **The roadmap** changes when a phase starts, changes scope or finishes, in the same PR as that work.
- **docs/guide.md** shows only commands and output that were actually run on the engine as it is.
  A change that alters an output in it updates the guide in the same PR.

## Skills and CLAUDE.md

- A skill is `.claude/skills/<name>/SKILL.md` with front matter `name: <name>` (the directory name)
  and a `description` that says when to load it. Its description is the whole trigger.
- Every skill has a line in the "Skills" list of CLAUDE.md, and every skill named there exists.
  Lint rule Z008 checks both.
- CLAUDE.md keeps only what applies to every task and the rules that bite before anyone knows they
  are in the situation. Anything else goes in the skill for its area.

## Rules need something that fails

A rule written only in prose is not followed. When a rule matters, back it with a compiler flag, a
lint rule in `tools/lint.py`, a git or Claude Code hook, a test or a CI gate, and add a row to
"Rules that are enforced" in CLAUDE.md naming what catches it.

## Claims

- Each statement comes from the source, a tool's `--help` output, or an actual run. Write the
  condition ("when X, Y happens"), not the story of one session.
- Point at code by file and symbol (`sim::kTickSeconds`, `MODULE_DEPS` in tools/lint.py), not by
  line number; line numbers go stale with the next edit.
