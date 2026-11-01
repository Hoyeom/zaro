# Your first ZARO scene

This walks through the whole loop: write a scene, validate it, run it, script it, test it, and
take a screenshot. Every command and output below was run on the engine as it is in this
repository. Paths are relative to the repository root; `zaro` is `build/release/zaro` (or
`build/debug/zaro`) after `python tools/build.py release`.

## 1. See what you can put in a scene

```sh
zaro schema
```

This prints every registered component as JSON: fields, types, defaults, allowed ranges and docs.
Agents should read it before writing a scene instead of guessing names. Today the components are
the crowd demo's: `CrowdArena`, `CrowdSpawner` and `PlayerController`.

## 2. Write a scene

Create `mygame/scene.toml`:

```toml
[scene]
name = "mygame"

[[entity]]
name = "arena"
[entity.CrowdArena]
grid_size = 128
seed = 7

[[entity]]
name = "horde"
[entity.CrowdSpawner]
count = 5000
speed = 3.0

[[entity]]
name = "player"
[entity.PlayerController]
speed = 8.0
```

Fields you leave out keep their defaults. Entities can also start from a prefab
(`prefab = "prefabs/player.toml"`); see `examples/crowd/`.

## 3. Validate it

```
$ zaro check mygame/scene.toml
mygame/scene.toml: ok (scene 'mygame', 3 entities, 3 components)
```

Every problem is reported in one pass, with file, line, column and a stable error id. In a copy
of the scene, `mygame/broken.toml`, misspell the horde's `speed` as `sped`:

```
$ zaro check mygame/broken.toml
mygame/broken.toml:14:8: error[ZE0402]: CrowdSpawner has no field 'sped' (did you mean 'speed'?)
```

For tools and agents, `--json` prints the same diagnostics as JSON:

```
$ zaro check mygame/broken.toml --json
[
 {"id": "ZE0402", "file": "mygame/broken.toml", "line": 14, "column": 8, "message": "CrowdSpawner has no field 'sped' (did you mean 'speed'?)"}
]
```

The error ids are listed in `src/core/log.h`.

## 4. Run it

```sh
zaro run mygame/scene.toml                      # a window: WASD or arrows to move, Esc quits
zaro run mygame/scene.toml --sim-only --warmup 0 --frames 600   # no window, no GPU
```

A headless run prints the simulation's state hash. The same scene, seed and tick count always
give the same hash, whatever the thread count (`--threads 1` prints the same line):

```
state_hash=3b7bc8351ec39836 ticks=600
```

`zaro run --headless --perf perf.json` renders offscreen and writes timings; see CLAUDE.md
"Headless runs and perf.json".

## 5. Add a script

Add `script = "game.luau"` to `[scene]`, and create `mygame/game.luau`:

```lua
-- Every 10 seconds the horde gets 1 m/s faster.
function on_start()
    zaro.log("horde of", zaro.get("horde", "CrowdSpawner", "count"))
end

function on_tick(tick)
    if tick % 600 == 0 then
        local speed = zaro.get("horde", "CrowdSpawner", "speed") + 1.0
        zaro.set("horde", "CrowdSpawner", "speed", speed)
        zaro.log("tick", tick, "horde speed", speed)
    end
end
```

```
$ zaro run mygame/scene.toml --sim-only --warmup 0 --frames 1200
script: horde of 5000
script: tick 600 horde speed 4
script: tick 1200 horde speed 5
state_hash=e7ca666785104b5d ticks=1200
```

`on_tick` runs at 60 Hz, before each simulation tick. `zaro.set` can change any field the schema
lists, and is checked like a scene file. In a window, saving `game.luau` reloads it while the game
runs. The full API and the sandbox rules are at the top of `src/script/script.h`.

## 6. Test it

Create `mygame/tests/speedup.test.toml`:

```toml
[test]
scene = "../scene.toml"
ticks = 1200

[[expect]]
entity = "horde"
component = "CrowdSpawner"
field = "speed"
equals = 5.0
```

```
$ zaro test mygame
PASS mygame/tests/speedup.test.toml (1200 ticks, state_hash e7ca666785104b5d)
zaro test: 1 passed, 0 failed
```

`--update` records the run's state hash in the file. From then on, any change to the simulation's
outcome fails the test until you accept it with `--update` again:

```
$ zaro test mygame --update
PASS mygame/tests/speedup.test.toml (1200 ticks, state_hash e7ca666785104b5d, golden updated)
```

A script error fails the test and points at the script line:

```
$ zaro test mygame
FAIL mygame/tests/speedup.test.toml (600 ticks, state_hash 3b7bc8351ec39836)
mygame/game.luau:9:1: error[ZE0501]: CrowdSpawner has no field 'sped' (did you mean 'speed'?)
zaro test: 0 passed, 1 failed
```

`zaro test --json` gives one JSON object per test with its diagnostics. Without paths, `zaro test`
looks in `tests/`. The file format is at the top of `src/scene/test_spec.h`.

## 7. Look at it

```
$ zaro screenshot mygame/scene.toml shot.png
wrote shot.png
```

This renders 60 frames offscreen (`--frames N`, `--size WxH`) and saves the last one. It needs a
GPU, unlike `zaro test`.

## Where to go next

- `examples/hello/` uses every `zaro.*` function once; `examples/crowd/` uses waves and a prefab.
- To add your own components, see CLAUDE.md "Scenes and components".
- The reasons behind these designs are in `docs/DECISIONS.md` (D-009, D-017).
