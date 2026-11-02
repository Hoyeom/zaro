// Sound files (D-032): the sounds a scene plays and the volume groups they play in. A scene names
// one with `sounds = "audio/sounds.toml"` in [scene]; `music = "<sound>"` there starts a track.
//
//   [[group]]                  # volume groups; "master" always exists and holds every other group
//   name = "music"
//   volume = 0.8               # optional, 0..1, default 1
//   save = "music_volume"      # optional: the volume follows this number field of the game's save
//                              # (D-033), so a settings screen sets the field and it is kept across runs
//
//   [[sound]]
//   name = "hit"               # required, unique in the file
//   file = "hit.wav"           # required, relative to this file: .wav, .ogg, .mp3 or .flac
//   group = "sfx"              # optional, a [[group]] name or "master" (the default)
//   volume = 0.7               # optional, 0..1, default 1
//   pitch = [0.95, 1.05]       # optional: each play picks a pitch in this range (1 is unchanged)
//   voices = 4                 # optional, 1..64, default 8: plays at once; a new one replaces the oldest
//   cooldown = 0.03            # optional, seconds: plays sooner than this after the last one are dropped
//   stream = true              # optional: decoded while it plays instead of when the scene loads (music)
//   loop = true                # optional: starts again at the end
//
// Instead of a file, a sound can be synthesized when the scene loads, so a game can sound finished
// with text alone. A tone:
//
//   [[sound]]
//   name = "zap"
//   synth = { wave = "square", from = 880, to = 220, time = 0.12, attack = 0.002, release = 0.05 }
//
// wave is sine, square, triangle, saw or noise; the frequency glides from `from` to `to` Hz (`to`
// defaults to `from`; for noise they are how often it changes); time is 0.005..10 seconds; attack and
// release (seconds, default 0.005 and 0.05) shape its start and end. Or a pattern of notes, such as a
// music loop:
//
//   [[sound]]
//   name = "theme"
//   group = "music"
//   loop = true
//   [sound.pattern]
//   step = 0.125               # seconds per step
//   tracks = [                 # played together; a shorter track repeats to the longest one's length
//     { wave = "saw", notes = "A2 . A2 - C3 . G2 -", volume = 0.5, attack = 0.005, release = 0.05 },
//     { wave = "noise", notes = "C8 . C8 .", volume = 0.2, release = 0.03 },
//   ]
//
// A note per step: "A2", "C#3", "Bb4" (octaves 0..8), "." for a rest, "-" to hold the note before
// one more step. A pattern lasts at most 60 seconds.
//
// The loader checks keys, names and ranges and that each file exists, with file:line diagnostics
// (ZE1300, ZE1301). Decoding is checked by the audio module (`zaro check` does it).
#pragma once

#include "core/diag.h"

#include <stdint.h>

namespace zaro::scene {

constexpr uint32_t kMaxSoundGroups = 16;  // including master
constexpr uint32_t kMaxSounds = 256;
constexpr uint32_t kMaxSynthTracks = 128;   // pattern tracks in one file
constexpr uint32_t kMaxSynthNotes = 16384;  // pattern notes in one file

enum SynthWave : uint8_t { kWaveSine, kWaveSquare, kWaveTriangle, kWaveSaw, kWaveNoise };
extern const char* const kSynthWaveNames[];  // by SynthWave, nullptr-terminated

struct SynthTone {
    uint8_t wave;  // SynthWave
    float from, to;  // Hz
    float time;
    float attack, release;
};

// A note of a pattern track: hz 0 is a rest; it lasts `steps` steps.
struct SynthNote {
    float hz;
    uint32_t steps;
};

struct SynthTrack {
    uint8_t wave;  // SynthWave
    float volume, attack, release;
    uint32_t first_note, note_count;  // into SoundBank::notes
    uint32_t steps;                   // the track's length in steps
};

struct SoundGroup {
    char name[64];
    float volume;
    int line;
    char save[64];  // a save field the volume follows, or empty
    int save_line;
};

struct Sound {
    enum Source : uint8_t { kFile, kTone, kPattern };
    char name[64];
    Source source;
    char file[260];  // kFile: the resolved path
    uint32_t group;  // index into SoundBank::groups; 0 is master
    float volume;
    float pitch_min, pitch_max;
    uint32_t voices;
    float cooldown;
    bool stream;
    bool loop;
    int line;
    SynthTone tone;  // kTone
    float step;      // kPattern: seconds per step
    uint32_t first_track, track_count;  // kPattern: into SoundBank::tracks
    uint32_t steps;  // kPattern: the longest track's steps
};

struct SoundBank {
    char path[260];
    SoundGroup groups[kMaxSoundGroups];  // groups[0] is master
    uint32_t group_count;
    Sound sounds[kMaxSounds];
    uint32_t sound_count;
    SynthTrack tracks[kMaxSynthTracks];
    uint32_t track_count;
    SynthNote notes[kMaxSynthNotes];
    uint32_t note_count;
};

// Loads and checks a sound file. Every problem goes to diags; returns nullptr if there was any.
SoundBank* load_sound_bank(const char* path, DiagList* diags);
void free_sound_bank(SoundBank* bank);

// Index of the named sound or group, or -1. The suggest_* functions give the closest name for
// "did you mean", or nullptr.
int find_sound(const SoundBank* bank, const char* name);
int find_sound_group(const SoundBank* bank, const char* name);
const char* suggest_sound(const SoundBank* bank, const char* name);
const char* suggest_sound_group(const SoundBank* bank, const char* name);

}  // namespace zaro::scene
