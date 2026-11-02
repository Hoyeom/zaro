---
name: zaro-audio
description: Sound in ZARO (D-032) — sound files with volume groups, file sounds and synthesized tones and note patterns, `[scene] sounds` and `music`, Button.sound, the `audio.*` script API, the request queue and the null mixer in headless runs and scene tests, the audio module on miniaudio and platform's SDL3 output, and the ZE1300-ZE1302 errors. Load before writing or changing a sound file, adding sound to a scene, script or screen, editing src/audio/, src/app/audio_layer or platform's audio output, and when a sound does not play.
---

# Sound (D-032)

## Files

- A scene names one sound file: `sounds = "audio/sounds.toml"` in `[scene]`, and optionally
  `music = "<sound>"`, which starts when the scene loads. The format is at the top of
  `src/scene/sound_bank.h`: `[[group]]`s under the implicit `master`, and `[[sound]]`s from a file
  (`.wav`, `.ogg`, `.mp3`, `.flac`), a synthesized tone (`synth = {...}`) or a note pattern
  (`[sound.pattern]`). Synthesized sounds are rendered once when the scene loads, so a game can
  sound finished with text alone (`examples/neon`).
- A `Button` with `sound = "<sound>"` plays it when clicked.
- A `[[group]]` with `save = "<field>"` follows that number field of the game's save, the settings
  hook (`zaro-save`); `zaro check` reports a field that is missing or not a number (ZE1302).
- `zaro check <scene>` loads the file, decodes the start of every file sound, and checks `music`
  and every `Button.sound` on the scene's screens: ZE1300 (keys, ranges, groups, notes), ZE1301 (a
  file missing, of another type, or not decodable), ZE1302 (a sound or group name the file does not
  have, with "did you mean").
- The Android export packs the sound file and every file it names.

## Scripts

`audio.play`, `audio.music`, `audio.track` and `audio.volume` are documented at the top of
`src/script/script.h`. They write requests into `scene::AudioState` (`src/scene/audio_state.h`);
the app hands them to the mixer after each tick and after the frame's UI hooks
(`app::flush_audio`). Nothing is read back from the mixer: `audio.volume` returns the value last
set and `audio.track` the track last asked for. Audio therefore never changes a run or a golden
hash (D-005). Misuse is ZE0501 with the script's line.

## Runs and tests

- Windowed runs mix through `platform::open_audio_output` (SDL3). Without an output device the
  run prints `no audio output (...); sound is off` and continues with a null mixer.
- Headless runs, `--sim-only` and scene tests use a null mixer: it decodes nothing, mixes silence
  and counts plays.
- Scene tests check `[[expect]] sound = "<name>"` (plays asked for, including the music) and
  `volume = "<group>"` with `equals`, `min` or `max` (`src/scene/test_spec.h`).
- To hear nothing while testing a window, set `SDL_AUDIO_DRIVER=dummy`. `SDL_AUDIO_DRIVER=disk`
  writes the mixed float samples to `sdlaudio.raw` in the working directory, which shows whether
  something was mixed.

## Code

- `src/audio/` is the only module that includes `miniaudio.h` (lint Z007) and may include only
  `core`. It takes plain data (`audio::SoundDesc`, samples, `audio::Tone`) and knows nothing of
  scenes; `app/audio_layer` converts the sound file and renders synthesized sounds with
  `audio::add_tone`.
- miniaudio is built with `MA_NO_DEVICE_IO` (`cmake/ZaroDeps.cmake`): the engine runs without a
  device and `audio::mix` pulls frames from it on SDL's audio thread. Every other `audio::`
  function runs on the main thread. Close the output before destroying the mixer.
- Each sound has a fixed set of voices (`voices`, default 8); a play takes a free voice or the
  oldest one, and `cooldown` drops plays that come too soon. A streamed file has one voice.
- On mobile, `app::pause_audio` stops the output while the app is in the background.
