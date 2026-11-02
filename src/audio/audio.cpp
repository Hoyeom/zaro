#include "audio/audio.h"

#include <miniaudio.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::audio {

namespace {

constexpr uint32_t kMaxSounds = 256;
constexpr uint32_t kMusicSlots = 4;  // the playing track and tracks still fading out
// Sounds are not placed in space; they keep their pitch control.
constexpr ma_uint32 kSoundFlags = MA_SOUND_FLAG_NO_SPATIALIZATION;

}  // namespace

// A sound of the mixer (outside the anonymous namespace: Mixer holds them).
struct Sound {
    SoundDesc desc;
    char path[260];
    float* samples;              // synthesized sounds: mono at kSampleRate; nullptr for files
    uint32_t frame_count;
    ma_audio_buffer_ref* refs;   // samples: one reader per voice
    ma_sound* voices;            // desc.voices plays; file voices share one decoded buffer
    uint64_t* started;           // per voice: the play counter when it started
    uint32_t voice_count;        // voices initialized
    double last_play_ms;
    bool played;
};

struct Mixer {
    bool null_mixer = false;
    ma_engine engine;
    ma_sound_group groups[kMaxGroups];  // [0] unused: master is the engine's own volume
    uint32_t group_count = 1;
    Sound sounds[kMaxSounds];
    uint32_t sound_count = 0;
    ma_sound music[kMusicSlots];
    ma_audio_buffer_ref music_refs[kMusicSlots];  // for tracks made of samples
    bool music_used[kMusicSlots] = {};
    int music_current = -1;  // slot of the playing track
    uint32_t rng = 0x9e3779b9u;
    MixerStats stats = {};
};

namespace {

ma_sound_group* group_of(Mixer* m, uint32_t group) {
    return group > 0 && group < m->group_count ? &m->groups[group] : nullptr;
}

uint32_t next_random(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

float random_pitch(Mixer* m, const SoundDesc& d) {
    if (d.pitch_max <= d.pitch_min) return d.pitch_min;
    return d.pitch_min + (d.pitch_max - d.pitch_min) * (float)(next_random(&m->rng) >> 8) / 16777216.0f;
}

// A sound that reads `s`'s samples through `ref`.
ma_result init_sample_sound(Mixer* m, Sound& s, ma_audio_buffer_ref* ref, ma_sound* out) {
    ma_result r = ma_audio_buffer_ref_init(ma_format_f32, 1, s.samples, s.frame_count, ref);
    if (r != MA_SUCCESS) return r;
    ref->sampleRate = kSampleRate;
    r = ma_sound_init_from_data_source(&m->engine, ref, kSoundFlags, group_of(m, s.desc.group), out);
    if (r != MA_SUCCESS) ma_audio_buffer_ref_uninit(ref);
    return r;
}

void free_sound(Sound& s) {
    for (uint32_t v = 0; v < s.voice_count; ++v) {
        ma_sound_uninit(&s.voices[v]);
        if (s.refs) ma_audio_buffer_ref_uninit(&s.refs[v]);
    }
    free(s.voices);
    free(s.refs);
    free(s.started);
    free(s.samples);
    s = Sound{};
}

// Creates the sound's voices. False with a message in err.
bool init_voices(Mixer* m, Sound& s, char* err, uint32_t err_size) {
    const uint32_t n = s.desc.voices;
    s.voices = (ma_sound*)calloc(n, sizeof(ma_sound));
    s.started = (uint64_t*)calloc(n, sizeof(uint64_t));
    ma_result r = MA_SUCCESS;
    if (s.samples) {
        s.refs = (ma_audio_buffer_ref*)calloc(n, sizeof(ma_audio_buffer_ref));
        for (; r == MA_SUCCESS && s.voice_count < n; ++s.voice_count) {
            r = init_sample_sound(m, s, &s.refs[s.voice_count], &s.voices[s.voice_count]);
            if (r != MA_SUCCESS) break;
        }
    } else {
        // The first voice decodes the file (or opens the stream); the others share its data.
        const ma_uint32 flags = kSoundFlags | (s.desc.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE);
        r = ma_sound_init_from_file(&m->engine, s.path, flags, group_of(m, s.desc.group), nullptr, &s.voices[0]);
        if (r == MA_SUCCESS) s.voice_count = 1;
        for (; r == MA_SUCCESS && s.voice_count < n; ++s.voice_count) {
            r = ma_sound_init_copy(&m->engine, &s.voices[0], kSoundFlags, group_of(m, s.desc.group),
                                   &s.voices[s.voice_count]);
            if (r != MA_SUCCESS) break;
        }
    }
    if (r != MA_SUCCESS) {
        snprintf(err, err_size, "cannot %s '%s': %s", s.samples ? "play" : "decode", s.path, ma_result_description(r));
        return false;
    }
    for (uint32_t v = 0; v < n; ++v) ma_sound_set_looping(&s.voices[v], s.desc.loop ? MA_TRUE : MA_FALSE);
    return true;
}

int add(Mixer* m, const SoundDesc& d, float* samples, uint32_t frames, char* err, uint32_t err_size) {
    if (m->sound_count == kMaxSounds) {
        free(samples);
        snprintf(err, err_size, "more than %u sounds", kMaxSounds);
        return -1;
    }
    Sound& s = m->sounds[m->sound_count];
    s = Sound{};
    s.desc = d;
    snprintf(s.path, sizeof(s.path), "%s", d.path ? d.path : "");
    s.desc.path = s.path;
    s.samples = samples;
    s.frame_count = frames;
    if (s.desc.voices == 0) s.desc.voices = 1;
    if (s.desc.stream && !samples) s.desc.voices = 1;  // each streamed voice would decode on its own
    if (!m->null_mixer && !init_voices(m, s, err, err_size)) {
        free_sound(s);
        return -1;
    }
    ++m->stats.sounds;
    return (int)m->sound_count++;
}

}  // namespace

Mixer* create_mixer(bool null_mixer) {
    Mixer* m = new Mixer();
    m->null_mixer = null_mixer;
    if (null_mixer) return m;
    ma_engine_config config = ma_engine_config_init();
    config.noDevice = MA_TRUE;  // platform's audio output pulls frames through mix()
    config.channels = kChannels;
    config.sampleRate = kSampleRate;
    if (ma_engine_init(&config, &m->engine) != MA_SUCCESS) {
        delete m;
        return nullptr;
    }
    return m;
}

void destroy_mixer(Mixer* m) {
    if (!m) return;
    if (!m->null_mixer) {
        for (uint32_t i = 0; i < kMusicSlots; ++i) {
            if (!m->music_used[i]) continue;
            ma_sound_uninit(&m->music[i]);
            ma_audio_buffer_ref_uninit(&m->music_refs[i]);
        }
        for (uint32_t i = 0; i < m->sound_count; ++i) free_sound(m->sounds[i]);
        for (uint32_t g = 1; g < m->group_count; ++g) ma_sound_group_uninit(&m->groups[g]);
        ma_engine_uninit(&m->engine);
    } else {
        for (uint32_t i = 0; i < m->sound_count; ++i) free(m->sounds[i].samples);
    }
    delete m;
}

bool is_null(const Mixer* m) { return m->null_mixer; }

int add_group(Mixer* m, float volume) {
    if (m->group_count == kMaxGroups) return -1;
    const uint32_t g = m->group_count;
    if (!m->null_mixer) {
        if (ma_sound_group_init(&m->engine, kSoundFlags, nullptr, &m->groups[g]) != MA_SUCCESS) return -1;
        ma_sound_group_set_volume(&m->groups[g], volume);
    }
    ++m->group_count;
    return (int)g;
}

int add_sound(Mixer* m, const SoundDesc& d, char* err, uint32_t err_size) {
    return add(m, d, nullptr, 0, err, err_size);
}

int add_sound_samples(Mixer* m, const SoundDesc& d, float* samples, uint32_t frames, char* err, uint32_t err_size) {
    if (!samples || !frames) {
        free(samples);
        snprintf(err, err_size, "'%s' has no samples", d.path ? d.path : "");
        return -1;
    }
    return add(m, d, samples, frames, err, err_size);
}

void play(Mixer* m, uint32_t sound, float volume, float pitch, float pan) {
    if (sound >= m->sound_count) return;
    Sound& s = m->sounds[sound];
    if (m->null_mixer) {
        ++m->stats.plays;
        return;
    }
    const double now = (double)ma_engine_get_time_in_milliseconds(&m->engine);
    if (s.played && s.desc.cooldown > 0.0f && now - s.last_play_ms < (double)s.desc.cooldown * 1000.0) {
        ++m->stats.dropped;
        return;
    }
    // A free voice, or else the one that started first.
    uint32_t pick = 0;
    bool free_voice = false;
    for (uint32_t v = 0; v < s.voice_count; ++v) {
        if (!ma_sound_is_playing(&s.voices[v])) {
            pick = v;
            free_voice = true;
            break;
        }
        if (s.started[v] < s.started[pick]) pick = v;
    }
    if (!free_voice) ++m->stats.stolen;
    ma_sound* voice = &s.voices[pick];
    ma_sound_stop(voice);
    ma_sound_seek_to_pcm_frame(voice, 0);
    ma_sound_set_volume(voice, s.desc.volume * volume);
    ma_sound_set_pitch(voice, pitch > 0.0f ? pitch : random_pitch(m, s.desc));
    ma_sound_set_pan(voice, pan);
    ma_sound_start(voice);
    s.started[pick] = ++m->stats.plays;
    s.last_play_ms = now;
    s.played = true;
}

void play_music(Mixer* m, int sound, float fade_seconds) {
    if (sound >= (int)m->sound_count) return;
    if (m->null_mixer) {
        if (sound >= 0) ++m->stats.plays;
        return;
    }
    const ma_uint64 fade_ms = fade_seconds > 0.0f ? (ma_uint64)(fade_seconds * 1000.0f) : 0;
    if (m->music_current >= 0) {
        ma_sound* old = &m->music[m->music_current];
        if (fade_ms) ma_sound_stop_with_fade_in_milliseconds(old, fade_ms);
        else ma_sound_stop(old);
        m->music_current = -1;
    }
    if (sound < 0) return;
    update(m);
    int slot = -1;
    for (uint32_t i = 0; i < kMusicSlots && slot < 0; ++i) {
        if (!m->music_used[i]) slot = (int)i;
    }
    if (slot < 0) {  // every slot is still fading: cut the first one short
        slot = 0;
        ma_sound_uninit(&m->music[0]);
        ma_audio_buffer_ref_uninit(&m->music_refs[0]);
        m->music_used[0] = false;
    }
    Sound& s = m->sounds[sound];
    ma_sound* track = &m->music[slot];
    ma_result r;
    m->music_refs[slot] = ma_audio_buffer_ref{};
    if (s.samples) {
        r = init_sample_sound(m, s, &m->music_refs[slot], track);
    } else {
        const ma_uint32 flags = kSoundFlags | (s.desc.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE);
        r = ma_sound_init_from_file(&m->engine, s.path, flags, group_of(m, s.desc.group), nullptr, track);
    }
    if (r != MA_SUCCESS) return;
    m->music_used[slot] = true;
    m->music_current = slot;
    ma_sound_set_looping(track, s.desc.loop ? MA_TRUE : MA_FALSE);
    if (fade_ms) ma_sound_set_fade_in_milliseconds(track, 0.0f, s.desc.volume, fade_ms);
    else ma_sound_set_volume(track, s.desc.volume);
    ma_sound_start(track);
    ++m->stats.plays;
}

void set_group_volume(Mixer* m, uint32_t group, float volume) {
    if (m->null_mixer || group >= m->group_count) return;
    if (group == 0) ma_engine_set_volume(&m->engine, volume);
    else ma_sound_group_set_volume(&m->groups[group], volume);
}

void update(Mixer* m) {
    if (m->null_mixer) return;
    for (uint32_t i = 0; i < kMusicSlots; ++i) {
        if (!m->music_used[i] || (int)i == m->music_current || ma_sound_is_playing(&m->music[i])) continue;
        ma_sound_uninit(&m->music[i]);
        ma_audio_buffer_ref_uninit(&m->music_refs[i]);
        m->music_used[i] = false;
    }
}

MixerStats stats(const Mixer* m) { return m->stats; }

void mix(Mixer* m, float* out, uint32_t frames) {
    if (m->null_mixer) {
        memset(out, 0, sizeof(float) * kChannels * frames);
        return;
    }
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&m->engine, out, frames, &read);
    if (read < frames) memset(out + read * kChannels, 0, sizeof(float) * kChannels * (size_t)(frames - read));
}

bool probe_file(const char* path, char* err, uint32_t err_size) {
    ma_decoder decoder;
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, kChannels, kSampleRate);
    ma_result r = ma_decoder_init_file(path, &config, &decoder);
    if (r == MA_SUCCESS) {
        float frames[256 * kChannels];
        ma_uint64 read = 0;
        r = ma_decoder_read_pcm_frames(&decoder, frames, 256, &read);
        if (r == MA_AT_END) r = MA_SUCCESS;  // a sound shorter than the probe
        ma_decoder_uninit(&decoder);
    }
    if (r != MA_SUCCESS) {
        snprintf(err, err_size, "cannot decode '%s': %s", path, ma_result_description(r));
        return false;
    }
    return true;
}

uint32_t frames_for(float seconds) {
    const double f = (double)seconds * kSampleRate;
    return f < 1.0 ? 1u : (uint32_t)f;
}

void add_tone(const Tone& t, float* out, uint32_t frames, uint32_t seed) {
    constexpr double kTwoPi = 6.283185307179586;
    const double rate = (double)kSampleRate;
    const double from = t.from_hz > 1.0f ? t.from_hz : 1.0, to = t.to_hz > 1.0f ? t.to_hz : 1.0;
    const double glide = log(to / from) / (double)frames;  // the frequency moves evenly in pitch
    const uint32_t attack = t.attack > 0.0f ? frames_for(t.attack) : 0;
    const uint32_t release = t.release > 0.0f ? frames_for(t.release) : 0;
    uint32_t rng = seed * 2654435761u + 0x6d2b79f5u;
    if (!rng) rng = 1;
    double phase = 0.0, hold = 0.0;
    float noise = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        const double hz = from * exp(glide * (double)i);
        float x = 0.0f;
        switch (t.wave) {
            case Wave::kSine: x = (float)sin(kTwoPi * phase); break;
            case Wave::kSquare: x = phase < 0.5 ? 1.0f : -1.0f; break;
            case Wave::kTriangle: x = (float)(4.0 * fabs(phase - 0.5) - 1.0); break;
            case Wave::kSaw: x = (float)(2.0 * phase - 1.0); break;
            case Wave::kNoise:
                hold += hz / rate;  // a new random value hz times a second
                if (i == 0 || hold >= 1.0) {
                    hold -= floor(hold);
                    noise = (float)(next_random(&rng) >> 8) / 8388608.0f - 1.0f;
                }
                x = noise;
                break;
        }
        phase += hz / rate;
        phase -= floor(phase);
        float env = 1.0f;
        if (i < attack) env = (float)i / (float)attack;
        if (frames - i <= release) {
            const float r = (float)(frames - i) / (float)release;
            if (r < env) env = r;
        }
        out[i] += x * env * t.volume;
    }
}

}  // namespace zaro::audio
