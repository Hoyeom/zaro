---
name: zaro-save
description: The local save (D-033) — save declarations with typed fields, merge hints and [[migrate]] steps, `[scene] save`, `save.get`/`save.set`/`save.commit` in scripts, where saves are stored (user data directory, `--save-dir`, memory in headless runs and tests), the atomic write with its backup, newer and damaged saves, starting saves and `save` expectations in scene tests, the store interface the backend plugin replaces, and ZE1350-ZE1353. Load before writing or changing a save declaration or a [[migrate]] step, before using `save.*` in a script, before editing src/scene/save_spec or src/app/save_layer, and when a save does not load or keep its values.
---

# Local save (D-033)

## Declaration and versions

- A scene names its save declaration with `save = "save.toml"` in `[scene]`. The format is at the
  top of `src/scene/save_spec.h`: `[save]` with `game` and `version`, a `[[field]]` per value
  (`int`, `number`, `bool`, `string`; default, range, `merge`, `local`) and `[[migrate]]` steps.
- Changing the fields of a released game: raise `version` and add a `[[migrate]] from = <old>`
  step that renames, removes or sets what changed. A new field needs no step; it gets its default.
  Keys no field declares are dropped with a ZE1351 diagnostic, so a forgotten rename shows up in a
  test that starts from an old save.
- Write `merge` and `local` now even though only the local store exists: the backend's store
  (P4c) uses them to merge copies and to keep device values (volumes) on the device.
- `zaro check <scene>` checks the declaration: ZE1350 with file and line.

## Scripts

`save.get`, `save.set` and `save.commit` are documented at the top of `src/script/script.h`.
Values are loaded before `on_start`. `save.set` is type- and range-checked like `zaro.set`
(ZE0501 with the line, "did you mean" for field names). Nothing is stored until `save.commit()`;
the app hands the values to the store after the hook that committed (`app::flush_save`), and once
more when the run ends. Commit at points where the save is consistent (a run ended, a setting
changed), not every tick.

A sound group with `save = "<number field>"` follows that field (the settings hook, `zaro-audio`):
a settings screen calls `save.set("music_volume", v)` and the music volume follows at once;
`save.commit()` keeps it for the next run. `audio.volume` on such a group is an error.

## Where it is stored

| Run | Store |
|---|---|
| windowed | `save.toml` in the game's user data directory (`platform::user_data_dir`: `%APPDATA%\zaro\<game>\` on Windows, `~/.local/share/zaro/<game>/` on Linux, internal storage on Android) |
| `--save-dir DIR` | `DIR/save.toml` |
| headless, `--sim-only`, scene tests | memory, starting from the defaults or from a test's `save = "<file>"` |

- A file store writes `save.toml.tmp`, flushes it to the disk, renames the old `save.toml` to
  `save.toml.bak` and the new file into place (`platform::write_file_atomic`). A missing or damaged
  `save.toml` loads the backup.
- A save written by a newer `version` is neither loaded nor overwritten (ZE1352): the game runs on
  defaults and its commits are refused, so a downgraded build cannot destroy progress.
- Problems with a player's save are printed (`zaro: error[ZE1351]` ...) and the game goes on. A
  failed write is ZE1353; the next commit tries again.

## Tests

- `[test] save = "old_save.toml"` starts the run from that file, migrated; any problem with it
  fails the test. That is how a migration is tested (`tests/data/save/tests/migrate_v1.test.toml`).
- `[[expect]] save = "<field>"` checks the value as last committed, so an uncommitted change
  fails the expectation (`src/scene/test_spec.h`).
- Across runs: `zaro run <scene> --sim-only --save-dir DIR` twice; the ctest `save_runs_*` tests do
  this with `tests/data/save`.

## Code

- `src/scene/save_spec` holds the declaration, `scene::SaveData` (the values scripts change),
  reading with migration and writing. It is pure and needs no files.
- `src/app/save_layer` holds the store interface (`app::SaveStore`: load, commit, destroy), the
  file and memory stores, and the layer a run owns. Another store (the backend plugin's) is a new
  `SaveStore`; declarations and scripts do not change.
