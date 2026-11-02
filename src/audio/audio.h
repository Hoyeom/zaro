// Sound mixing (D-032): sounds from audio files or synthesized samples, played in volume groups and
// mixed into float frames that the platform's audio output pulls (platform::open_audio_output).
// Built on miniaudio, which only this module includes. It takes plain data and knows nothing of
// scenes or scripts.
//
// Threads: every function except mix() is called from the main thread; mix() runs on the audio
// thread. Close the audio output before destroying the mixer.
//
// A null mixer (headless runs, scene tests, no output device) decodes nothing and mixes silence;
// it only counts what it was asked to play.
#pragma once

#include <stdint.h>

namespace zaro::audio {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kMaxGroups = 16;  // including master, group 0

struct Mixer;

struct SoundDesc {
    const char* path;      // the file; for samples only a name for messages
    uint32_t group = 0;
    float volume = 1.0f;
    float pitch_min = 1.0f, pitch_max = 1.0f;
    uint32_t voices = 8;   // at most this many plays at once; a new one takes over the oldest
    float cooldown = 0.0f;  // seconds; plays sooner after the last one are dropped
    bool stream = false;   // files: decoded while playing (one voice), not up front
    bool loop = false;
};

struct MixerStats {
    uint64_t plays;    // plays started
    uint64_t dropped;  // plays dropped by a cooldown
    uint64_t stolen;   // plays that took over a voice that was still playing
    uint32_t sounds;   // sounds added
};

// Group 0 (master) exists from the start. Returns nullptr if the mixer cannot start (then use a
// null one).
Mixer* create_mixer(bool null_mixer);
void destroy_mixer(Mixer* mixer);
bool is_null(const Mixer* mixer);

// A new group under master; returns its index, or -1 past kMaxGroups.
int add_group(Mixer* mixer, float volume);
// Loads a sound file (decodes it now unless it streams). Returns its index, or -1 with a message in err.
int add_sound(Mixer* mixer, const SoundDesc& desc, char* err, uint32_t err_size);
// A sound from `frames` mono samples at kSampleRate, allocated with malloc; the mixer frees them
// (also on failure). Returns its index, or -1 with a message in err.
int add_sound_samples(Mixer* mixer, const SoundDesc& desc, float* samples, uint32_t frames, char* err,
                      uint32_t err_size);

// pitch 0 picks one in the sound's range. Out-of-range indices are ignored.
void play(Mixer* mixer, uint32_t sound, float volume, float pitch, float pan);
// Switches the music to `sound` (-1: none), fading the old track out and the new one in.
void play_music(Mixer* mixer, int sound, float fade_seconds);
void set_group_volume(Mixer* mixer, uint32_t group, float volume);
// Frees music tracks that have faded out. Call once a frame.
void update(Mixer* mixer);
MixerStats stats(const Mixer* mixer);

// Audio thread: writes `frames` interleaved frames of kChannels floats.
void mix(Mixer* mixer, float* out, uint32_t frames);

// Opens and decodes the start of a file, as `zaro check` does. False with a message in err.
bool probe_file(const char* path, char* err, uint32_t err_size);

// --- Synthesis: tones rendered once into mono samples at kSampleRate.

enum class Wave : uint8_t { kSine, kSquare, kTriangle, kSaw, kNoise };

struct Tone {
    Wave wave = Wave::kSine;
    float from_hz = 440.0f;  // frequency at the start; noise: how often it picks a new value
    float to_hz = 440.0f;    // at the end, gliding evenly in pitch
    float attack = 0.005f;   // seconds rising from silence
    float release = 0.05f;   // seconds falling to silence at the end
    float volume = 1.0f;
};

// Samples for a duration, at least 1.
uint32_t frames_for(float seconds);
// Adds a tone filling `frames` samples to out. The same tone and seed give the same samples.
void add_tone(const Tone& tone, float* out, uint32_t frames, uint32_t seed);

}  // namespace zaro::audio
