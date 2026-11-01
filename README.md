<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/logo-dark.svg">
    <img alt="ZARO" src="docs/assets/logo.svg" width="320">
  </picture>
</p>

**Z**ero-royalty, **A**I-native, **R**apid, **O**pen game engine.

ZARO is a small, royalty-free engine designed to be easy for AI agents (and people) to drive:

- **Text everything**: scenes, prefabs and materials are TOML files referenced by path. No hidden GUIDs, git-friendly diffs.
- **CLI-first**: `zaro run --headless`, `zaro screenshot`, `zaro test` and JSON perf reports. The editor is a UI on top of the CLI.
- **Built for crowds**: GPU instancing, compute-shader particles, indirect draw, GPU culling, flow-field movement and spatial-hash collision for tens of thousands of entities.
- **Fast iteration**: compile in seconds, hot-reload Luau scripts and Slang shaders.

> **Status: pre-alpha. Developed in the open, not announced yet.** Expect breaking changes on
> any commit and no support. M1 is done: a crowd demo where 50,000 enemies swarm the player
> runs at several hundred fps. M2 is done: TOML scenes, Luau scripts, `zaro check`, `zaro test`
> and `zaro screenshot`; start with [docs/guide.md](docs/guide.md). There is no editor or game
> UI yet. External code contributions are not accepted yet; issues are welcome (see
> [CONTRIBUTING.md](CONTRIBUTING.md)).

## Build

Requirements: CMake ≥ 3.28, Ninja, a C++20 compiler, Python 3. On Windows, Visual Studio 2022 Build Tools are enough: `tools/build.py` finds MSVC and the CMake/Ninja bundled with them.

```sh
python tools/build.py release
build/release/zaro run examples/crowd/scene.toml                          # window: WASD, Esc quits
build/release/zaro run examples/crowd/scene.toml --headless --perf perf.json  # benchmark, no window
```

The first build downloads pinned dependencies (SDL3, wgpu-native, Slang, enkiTS) into `.cache/deps/`.
A GPU with compute shaders is required; `zaro run --sim-only` runs the simulation without one.

## Roadmap

1. ~~**M1**: 50,000 enemies chase the player at 60 fps; a headless run writes `perf.json` (fps, draw calls, entity count, GPU time).~~ Done, see [docs/DECISIONS.md](docs/DECISIONS.md#m1-result-2026-11-01).
2. ~~**CI**: GitHub Actions builds and tests on Windows, Linux and macOS.~~ Done. Performance-regression gating is still open.
3. ~~**M2, agent workflow**: TOML scenes and prefabs, Luau game scripts, `zaro screenshot` and `zaro test`, component schema export, docs and examples.~~ Done, see [docs/guide.md](docs/guide.md). The Luau type checker is still to come.
4. **Multi-platform foundations** (D-018): mobile builds (Android first), a UI system with Korean text, 2D sprites, and live services through a private backend plugin. The phases are in [docs/ROADMAP.md](docs/ROADMAP.md).
5. **Launch**: the engine is announced together with the first part of the owner's mobile game built on it (D-016, D-018).
6. **Later**: iOS store release, a WebGPU build, a crowd-action game, Steamworks.

Consoles are supported through private porting layers outside this repository (NDA).
Design kept open for later: gamepad/touch input, CJK fonts and IME, localization, audio, game UI, save-format versioning, physics, a local MCP server.

Design decisions: [docs/DECISIONS.md](docs/DECISIONS.md). Contributor and agent guide: [CLAUDE.md](CLAUDE.md).

## License

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE))
- MIT license ([LICENSE-MIT](LICENSE-MIT))

at your option.

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in the work by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any additional terms or conditions.
