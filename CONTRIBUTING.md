# Contributing to ZARO

## Current status: issues welcome, code contributions not yet

ZARO has a single maintainer and has not been announced. Until the launch (D-016), pull
requests from outside the project are not accepted. Bug reports, questions and ideas in issues
are welcome. Accepting no outside code for now keeps the licensing options open while the
project's direction settles. Whether contributions will require a CLA is decided when they open.

The rest of this file is the workflow that the maintainer and their agents follow. It will apply
to everyone once contributions open.

Read [CLAUDE.md](CLAUDE.md) first: it has the build commands and the rules the build and lint
enforce. It applies to humans and AI agents alike.

## Developer Certificate of Origin (DCO)

ZARO uses the [Developer Certificate of Origin 1.1](https://developercertificate.org/)
instead of a CLA. Every commit must carry a sign-off line certifying that you wrote the change
or otherwise have the right to submit it under the project's licenses:

```
Signed-off-by: Your Name <you@example.com>
```

`git commit -s` adds it automatically. The name and email must match the commit author.
Commits without a sign-off cannot be merged.

If an AI agent produced the change, you, the human submitting it, are the one signing off. You
are certifying that you have reviewed the change and have the right to contribute it.

## Workflow

1. Enable the git hooks once per clone: `git config core.hooksPath tools/githooks`
   (lint, branch and commit-message checks, no pushes to `main`).
2. Branch from `main` as `type/short-name`, e.g. `feat/flow-field-lod`.
3. Commit as `type(scope): subject` with a sign-off (`git commit -s`). See CLAUDE.md
   "Branches, commits and pull requests" for types and scopes.
4. Run `python tools/build.py debug --test`; it must pass.
5. Open a pull request whose title follows the same format. It is squash-merged, so the title
   becomes the commit on `main`. Describe what changed and why, and link a decision in
   `docs/DECISIONS.md` if the change touches architecture (or add a new decision entry).

## Dependencies

New third-party code must be MIT, BSD, zlib, Apache-2.0 or similarly permissive (D-012).
GPL, LGPL and AGPL code is not accepted. Proprietary SDKs belong in separate plugin
repositories, never in the engine core.

## License of contributions

Unless you state otherwise, contributions are dual licensed MIT OR Apache-2.0, as described
in [README.md](README.md#license).
