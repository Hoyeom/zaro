# Security policy

## Supported versions

ZARO is pre-alpha. Only the latest commit on the default branch receives fixes.

## Reporting a vulnerability

Please do not open a public issue for security problems. Report them privately through
GitHub: **Security → Report a vulnerability** on this repository. We aim to acknowledge reports
within 7 days and agree on a disclosure timeline with you.

Areas of particular interest:

- the script sandbox (Luau): escaping the sandbox from game scripts
- asset and scene loading: malformed glTF, KTX2 or TOML files
- the native plugin loader and plugin manifests
- the local MCP server, once it exists: it must never be reachable from outside the machine
