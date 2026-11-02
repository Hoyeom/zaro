// Audio (D-032): sound files with diagnostics by id and line, synthesized tones and patterns, the
// request queue scripts fill, and the mixer without an output device (null and real). Runs from
// the repository root (tests/data/audio).
#include "audio/audio.h"
#include "core/diag.h"
#include "scene/audio_state.h"
#include "scene/scene.h"
#include "scene/sound_bank.h"
#include "scene/world.h"
#include "script/script.h"
#include "sim/crowd_components.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace zaro;

namespace {

int g_failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

bool has_diag(const DiagList* d, ErrorId id, int line, const char* text = nullptr) {
    for (uint32_t i = 0; i < d->count; ++i) {
        if (d->items[i].id == id && d->items[i].line == line && (!text || strstr(d->items[i].message, text))) return true;
    }
    return false;
}

float peak(const float* x, uint32_t n) {
    float p = 0.0f;
    for (uint32_t i = 0; i < n; ++i) p = fabsf(x[i]) > p ? fabsf(x[i]) : p;
    return p;
}

void test_bank() {
    DiagList* d = new DiagList();
    scene::SoundBank* b = scene::load_sound_bank("tests/data/audio/sounds.toml", d);
    expect(b && d->count == 0, "sounds.toml loads");
    if (b) {
        expect(b->group_count == 3 && strcmp(b->groups[0].name, "master") == 0, "master plus two groups");
        expect(b->groups[1].volume == 0.8f, "group volume");
        const int click = scene::find_sound(b, "click"), blip = scene::find_sound(b, "blip");
        const int theme = scene::find_sound(b, "theme");
        expect(click == 0 && blip == 1 && theme == 2, "sounds by name");
        expect(b->sounds[click].source == scene::Sound::kFile && strstr(b->sounds[click].file, "tests/data/audio/click.wav"),
               "file resolved against the sound file");
        expect(b->sounds[click].voices == 2 && b->sounds[click].group == 1, "voices and group");
        const scene::Sound& t = b->sounds[blip];
        expect(t.source == scene::Sound::kTone && t.tone.wave == scene::kWaveSquare && t.tone.from == 880.0f &&
                   t.tone.to == 440.0f && t.tone.time == 0.08f && t.tone.release == 0.05f,
               "tone with its default release");
        expect(t.pitch_min == 0.9f && t.pitch_max == 1.1f && t.cooldown == 0.5f, "pitch range and cooldown");
        const scene::Sound& p = b->sounds[theme];
        expect(p.source == scene::Sound::kPattern && p.track_count == 2 && p.step == 0.125f && p.loop, "pattern");
        // "A2 - . C3 | A2 . G2 -": A2 for two steps, a rest, C3, A2, a rest, G2 for two steps.
        const scene::SynthTrack& bass = b->tracks[p.first_track];
        expect(bass.steps == 8 && bass.note_count == 6, "a hold extends the note before");
        expect(b->notes[bass.first_note].steps == 2 && b->notes[bass.first_note + 1].hz == 0.0f, "held note and rest");
        expect(fabsf(b->notes[bass.first_note].hz - 110.0f) < 0.01f, "A2 is 110 Hz");
        expect(fabsf(b->notes[bass.first_note + 2].hz - 130.81f) < 0.01f, "C3 is 130.81 Hz");
        expect(p.steps == 8, "the longest track sets the length");
        expect(scene::find_sound_group(b, "music") == 2, "group by name");
        const char* hint = scene::suggest_sound(b, "blipp");
        expect(hint && strcmp(hint, "blip") == 0, "did you mean blip");
    }
    scene::free_sound_bank(b);

    d->count = 0;
    b = scene::load_sound_bank("tests/data/audio/bad.toml", d);
    expect(b == nullptr, "bad.toml fails");
    expect(has_diag(d, kErrSoundFile, 4, "loudness"), "unknown group key, line 4");
    expect(has_diag(d, kErrSoundAsset, 8, "cannot open"), "missing file, line 8");
    expect(has_diag(d, kErrSoundRef, 13, "did you mean 'sfx'"), "unknown group with a hint, line 13");
    expect(has_diag(d, kErrSoundFile, 17, "wave must be"), "unknown wave, line 17");
    expect(has_diag(d, kErrSoundFile, 23, "'H4' is not a note"), "bad note, line 23");
    expect(has_diag(d, kErrSoundFile, 25, "one of file, synth or pattern"), "two sources, line 25");
    expect(has_diag(d, kErrSoundAsset, 32, "not a .wav"), "unsupported extension, line 32");
    if (g_failures) diag_print_text(d, stderr);
    delete d;
}

void test_state() {
    DiagList* d = new DiagList();
    scene::SoundBank* b = scene::load_sound_bank("tests/data/audio/sounds.toml", d);
    if (!b) {
        expect(false, "sounds.toml loads for the state test");
        delete d;
        return;
    }
    scene::AudioState* s = scene::create_audio_state(b);
    expect(s->group_volume[1] == 0.8f && s->music == -1, "starts at the file's volumes, no music");
    scene::audio_play(s, 1, 1.0f, 0.0f, 0.0f);
    scene::audio_play(s, 1, 1.0f, 0.0f, 0.0f);
    expect(s->play_count[1] == 2 && s->queue_count == 2, "plays counted and queued");
    scene::audio_music(s, 2, 0.5f);
    scene::audio_music(s, 2, 0.5f);
    expect(s->music == 2 && s->play_count[2] == 1 && s->queue_count == 3, "asking for the same track keeps it");
    scene::audio_music(s, -1, 0.0f);
    expect(s->music == -1 && s->queue[3].kind == scene::AudioCommand::kStopMusic, "music stopped");
    scene::set_group_volume(s, 2, 1.5f);
    expect(s->group_volume[2] == 1.0f && s->volume_changes == 1, "volume clamped to 0..1");
    s->queue_count = 0;
    for (uint32_t i = 0; i < scene::kMaxAudioCommands + 10; ++i) scene::audio_play(s, 0, 1.0f, 0.0f, 0.0f);
    expect(s->queue_count == scene::kMaxAudioCommands && s->dropped == 10 && s->play_count[0] == scene::kMaxAudioCommands + 10,
           "a full queue drops plays but counts them");
    scene::audio_music(s, 2, 0.0f);
    expect(s->queue[scene::kMaxAudioCommands - 1].kind == scene::AudioCommand::kMusic, "music still gets in");
    scene::destroy_audio_state(s);
    scene::free_sound_bank(b);
    delete d;
}

void test_tone() {
    // A 1 kHz sine for 0.1 s crosses zero about 200 times.
    const uint32_t n = audio::frames_for(0.1f);
    float* x = (float*)calloc(n, sizeof(float));
    audio::Tone t;
    t.from_hz = t.to_hz = 1000.0f;
    t.attack = t.release = 0.0f;
    audio::add_tone(t, x, n, 1);
    uint32_t crossings = 0;
    for (uint32_t i = 1; i < n; ++i) crossings += (x[i - 1] < 0.0f) != (x[i] < 0.0f);
    expect(n == 4800 && crossings >= 198 && crossings <= 202, "sine frequency");
    expect(peak(x, n) > 0.99f && peak(x, n) <= 1.0f, "full volume");
    // The release fades the end to silence, the attack starts from it.
    memset(x, 0, n * sizeof(float));
    t.wave = audio::Wave::kSquare;
    t.attack = 0.01f;
    t.release = 0.02f;
    t.volume = 0.5f;
    audio::add_tone(t, x, n, 1);
    expect(fabsf(x[0]) < 0.01f && fabsf(x[n - 1]) < 0.01f && peak(x, n) == 0.5f, "envelope and volume");
    // Noise is the same for the same seed.
    float* y = (float*)calloc(n, sizeof(float));
    memset(x, 0, n * sizeof(float));
    t.wave = audio::Wave::kNoise;
    t.from_hz = t.to_hz = 48000.0f;
    audio::add_tone(t, x, n, 7);
    audio::add_tone(t, y, n, 7);
    expect(memcmp(x, y, n * sizeof(float)) == 0 && peak(x, n) > 0.1f, "seeded noise");
    free(x);
    free(y);
}

audio::SoundDesc tone_desc(uint32_t voices, float cooldown) {
    audio::SoundDesc d;
    d.path = "tone";
    d.voices = voices;
    d.cooldown = cooldown;
    return d;
}

constexpr uint32_t kToneFrames = 9600;  // 0.2 s

float* tone_samples() {
    float* x = (float*)calloc(kToneFrames, sizeof(float));
    audio::add_tone(audio::Tone{}, x, kToneFrames, 1);
    return x;
}

void test_mixer() {
    char err[400];
    // Null: counts, mixes silence.
    audio::Mixer* m = audio::create_mixer(true);
    expect(m && audio::is_null(m), "null mixer");
    expect(audio::add_sound_samples(m, tone_desc(1, 0.0f), tone_samples(), kToneFrames, err, sizeof(err)) == 0,
           "null mixer takes a sound");
    audio::play(m, 0, 1.0f, 0.0f, 0.0f);
    float out[1024 * audio::kChannels];
    audio::mix(m, out, 1024);
    expect(audio::stats(m).plays == 1 && peak(out, 2048) == 0.0f, "null mixer: one play, silence");
    audio::destroy_mixer(m);

    // Real, without a device: what is played comes out of mix().
    m = audio::create_mixer(false);
    expect(m && !audio::is_null(m), "mixer without a device");
    if (!m) return;
    const int g = audio::add_group(m, 1.0f);
    audio::SoundDesc two = tone_desc(2, 0.0f);
    two.group = (uint32_t)g;
    expect(audio::add_sound_samples(m, two, tone_samples(), kToneFrames, err, sizeof(err)) == 0, "tone with two voices");
    expect(audio::add_sound_samples(m, tone_desc(1, 10.0f), tone_samples(), kToneFrames, err, sizeof(err)) == 1,
           "tone with a cooldown");
    audio::SoundDesc file;
    file.path = "tests/data/audio/click.wav";
    expect(audio::add_sound(m, file, err, sizeof(err)) == 2, "click.wav decodes");
    audio::SoundDesc bad;
    bad.path = "tests/data/audio/not_audio.wav";
    expect(audio::add_sound(m, bad, err, sizeof(err)) == -1 && strstr(err, "cannot decode"), "text is not audio");

    audio::mix(m, out, 1024);
    expect(peak(out, 2048) == 0.0f, "silence before a play");
    audio::play(m, 0, 1.0f, 1.0f, 0.0f);
    audio::mix(m, out, 1024);
    expect(peak(out, 2048) > 0.3f, "a play is heard");
    audio::play(m, 0, 1.0f, 1.0f, 0.0f);
    audio::play(m, 0, 1.0f, 1.0f, 0.0f);
    expect(audio::stats(m).stolen == 1, "a third play takes over the oldest of two voices");
    audio::play(m, 1, 1.0f, 1.0f, 0.0f);
    audio::play(m, 1, 1.0f, 1.0f, 0.0f);
    expect(audio::stats(m).dropped == 1, "a play within the cooldown is dropped");
    audio::play(m, 2, 1.0f, 1.0f, 0.0f);
    audio::mix(m, out, 1024);
    expect(peak(out, 2048) > 0.3f, "voices mix");

    // A muted group, then a muted master.
    audio::set_group_volume(m, (uint32_t)g, 0.0f);
    audio::set_group_volume(m, 0, 0.0f);
    for (int i = 0; i < 8; ++i) audio::mix(m, out, 1024);  // past the volume smoothing
    expect(peak(out, 2048) < 0.001f, "muted");
    audio::set_group_volume(m, 0, 1.0f);

    // Music: a looping track keeps playing; switching fades the old one out and frees it.
    audio::SoundDesc loop = tone_desc(1, 0.0f);
    loop.loop = true;
    expect(audio::add_sound_samples(m, loop, tone_samples(), kToneFrames, err, sizeof(err)) == 3, "loop");
    audio::play_music(m, 3, 0.0f);
    for (int i = 0; i < 20; ++i) audio::mix(m, out, 1024);  // 0.43 s: past the 0.2 s sample
    expect(peak(out, 2048) > 0.3f, "the music loops");
    audio::play_music(m, -1, 0.05f);
    for (int i = 0; i < 8; ++i) audio::mix(m, out, 1024);
    audio::update(m);
    expect(peak(out, 2048) < 0.001f, "the music faded out");
    audio::destroy_mixer(m);

    expect(audio::probe_file("tests/data/audio/click.wav", err, sizeof(err)), "probe a WAV");
    expect(!audio::probe_file("tests/data/audio/not_audio.wav", err, sizeof(err)), "probe text");
}

void test_script() {
    DiagList* d = new DiagList();
    scene::SoundBank* b = scene::load_sound_bank("tests/data/audio/sounds.toml", d);
    scene::Scene* sc = scene::load_scene("tests/data/audio/scene.toml", d);
    expect(b && sc, "scene and sounds load for the script test");
    if (!b || !sc) {
        if (d->count) diag_print_text(d, stderr);
        scene::free_sound_bank(b);
        scene::free_scene(sc);
        delete d;
        return;
    }
    expect(strstr(sc->sounds, "tests/data/audio/sounds.toml") && strcmp(sc->music, "theme") == 0, "[scene] sounds and music");
    scene::AudioState* a = scene::create_audio_state(b);
    script::ScriptDesc desc;
    desc.path = "tests/data/audio/audio.luau";
    desc.scene = sc;
    desc.audio = a;
    script::Script* s = script::load_script(desc, d);
    expect(s && script::call_start(s, d), "audio.luau starts");
    expect(a->group_volume[1] == 0.5f, "audio.volume sets a group");
    for (uint64_t t = 1; s && t <= 120; ++t) script::call_tick(s, t, d);
    expect(a->play_count[1] == 2 && a->queue[0].volume == 0.8f && a->queue[0].pan == -0.5f, "audio.play with options");
    script::destroy_script(s);

    // Misuse is a script error with the line and a hint.
    const char* path = "tests/data/audio/typo.luau";
    FILE* f = fopen(path, "wb");
    if (f) {
        fputs("function on_start()\n    audio.play(\"blipp\")\nend\n", f);
        fclose(f);
    }
    desc.path = path;
    d->count = 0;
    s = script::load_script(desc, d);
    expect(s && !script::call_start(s, d), "a misspelt sound fails");
    expect(has_diag(d, kErrScriptRuntime, 2, "no sound 'blipp' (did you mean 'blip'?)"), "ZE0501 at line 2 with a hint");
    script::destroy_script(s);
    remove(path);

    // Without sounds, audio.* says what is missing.
    desc.audio = nullptr;
    f = fopen(path, "wb");
    if (f) {
        fputs("function on_start()\n    audio.volume(\"sfx\", 1)\nend\n", f);
        fclose(f);
    }
    d->count = 0;
    s = script::load_script(desc, d);
    expect(s && !script::call_start(s, d) && has_diag(d, kErrScriptRuntime, 2, "the scene has no sounds"),
           "audio.* without sounds");
    script::destroy_script(s);
    remove(path);

    scene::destroy_audio_state(a);
    scene::free_scene(sc);
    scene::free_sound_bank(b);
    delete d;
}

}  // namespace

int main() {
    sim::register_crowd_components();
    scene::register_scene_components();
    test_bank();
    test_state();
    test_tone();
    test_mixer();
    test_script();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("audio: all tests passed\n");
    return 0;
}
