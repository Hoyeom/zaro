#include "app/audio_layer.h"

#include "audio/audio.h"
#include "core/log.h"
#include "core/reflect.h"
#include "platform/platform.h"
#include "scene/audio_state.h"
#include "scene/save_spec.h"
#include "scene/scene.h"
#include "scene/sound_bank.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

struct AudioLayer {
    scene::SoundBank* bank = nullptr;
    scene::AudioState* state = nullptr;
    audio::Mixer* mixer = nullptr;
    platform::AudioOutput* output = nullptr;
    uint32_t volume_changes = 0;  // AudioState::volume_changes the mixer has seen
    const scene::SaveData* save = nullptr;
    int save_field[scene::kMaxSoundGroups] = {};  // per group: the save field it follows, or -1
    uint32_t save_changes = 0;                    // SaveData::changes last applied
};

namespace {

void mix_output(float* out, uint32_t frames, void* user) { audio::mix((audio::Mixer*)user, out, frames); }

// [scene] music must name a sound of the file. Adds a diagnostic and returns -1 otherwise.
int music_sound(const scene::Scene* s, const scene::SoundBank* bank, DiagList* diags) {
    if (!s->music[0]) return -1;
    const int i = scene::find_sound(bank, s->music);
    if (i < 0) {
        const char* hint = scene::suggest_sound(bank, s->music);
        diag_add(diags, kErrSoundRef, s->path, s->music_line, 1, "music: %s has no sound '%s'%s%s%s", bank->path,
                 s->music, hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
    }
    return i;
}

audio::Tone tone_of(uint8_t wave, float from, float to, float attack, float release, float volume) {
    audio::Tone t;
    t.wave = (audio::Wave)wave;
    t.from_hz = from;
    t.to_hz = to;
    t.attack = attack;
    t.release = release;
    t.volume = volume;
    return t;
}

// Renders a synthesized sound (a tone or a pattern) into mono samples; the caller owns them.
float* render_synth(const scene::SoundBank* bank, const scene::Sound& s, uint32_t* frames) {
    if (s.source == scene::Sound::kTone) {
        const scene::SynthTone& t = s.tone;
        *frames = audio::frames_for(t.time);
        float* out = (float*)calloc(*frames, sizeof(float));
        audio::add_tone(tone_of(t.wave, t.from, t.to, t.attack, t.release, 1.0f), out, *frames, 1);
        return out;
    }
    // A pattern: every track repeats to the longest one's length; each note is a tone of its steps.
    const uint32_t step_frames = audio::frames_for(s.step);
    *frames = step_frames * s.steps;
    float* out = (float*)calloc(*frames, sizeof(float));
    uint32_t seed = 1;
    for (uint32_t k = 0; k < s.track_count; ++k) {
        const scene::SynthTrack& tr = bank->tracks[s.first_track + k];
        for (uint32_t step = 0; step < s.steps;) {
            for (uint32_t n = 0; n < tr.note_count && step < s.steps; ++n) {
                const scene::SynthNote& note = bank->notes[tr.first_note + n];
                const uint32_t steps = note.steps < s.steps - step ? note.steps : s.steps - step;
                if (note.hz > 0.0f) {
                    audio::add_tone(tone_of(tr.wave, note.hz, note.hz, tr.attack, tr.release, tr.volume),
                                    out + (size_t)step * step_frames, steps * step_frames, seed++);
                }
                step += steps;
            }
            if (!tr.note_count) break;
        }
    }
    return out;
}

// Builds the mixer's groups and sounds from the file. False (with ZE1301) if a sound does not decode.
bool fill_mixer(audio::Mixer* m, const scene::SoundBank* bank, DiagList* diags) {
    for (uint32_t g = 1; g < bank->group_count; ++g) audio::add_group(m, bank->groups[g].volume);
    audio::set_group_volume(m, 0, bank->groups[0].volume);
    bool ok = true;
    for (uint32_t i = 0; i < bank->sound_count; ++i) {
        const scene::Sound& s = bank->sounds[i];
        audio::SoundDesc d;
        d.path = s.file;
        d.group = s.group;
        d.volume = s.volume;
        d.pitch_min = s.pitch_min;
        d.pitch_max = s.pitch_max;
        d.voices = s.voices;
        d.cooldown = s.cooldown;
        d.stream = s.stream;
        d.loop = s.loop;
        char err[400];
        int added;
        if (s.source == scene::Sound::kFile) {
            added = audio::add_sound(m, d, err, sizeof(err));
        } else {
            d.path = s.name;
            uint32_t frames = 0;
            float* samples = audio::is_null(m) ? (float*)calloc(1, sizeof(float)) : render_synth(bank, s, &frames);
            added = audio::add_sound_samples(m, d, samples, frames ? frames : 1, err, sizeof(err));
        }
        if (added < 0) {
            diag_add(diags, kErrSoundAsset, bank->path, s.line, 1, "sound '%s': %s", s.name, err);
            ok = false;
        }
    }
    return ok;
}

}  // namespace

AudioLayer* create_audio_layer(const scene::Scene* scene, bool null_output, DiagList* diags, bool* failed) {
    *failed = false;
    if (!scene->sounds[0]) return nullptr;
    scene::SoundBank* bank = scene::load_sound_bank(scene->sounds, diags);
    const int music = bank ? music_sound(scene, bank, diags) : -1;
    if (!bank || (scene->music[0] && music < 0)) {
        scene::free_sound_bank(bank);
        *failed = true;
        return nullptr;
    }
    AudioLayer* l = new AudioLayer();
    l->bank = bank;
    l->state = scene::create_audio_state(bank);
    if (!null_output) {
        l->mixer = audio::create_mixer(false);
        if (l->mixer) l->output = platform::open_audio_output(audio::kSampleRate, audio::kChannels, mix_output, l->mixer);
        if (!l->output) {  // no device: count plays instead of mixing them
            audio::destroy_mixer(l->mixer);
            l->mixer = nullptr;
        }
    }
    if (!l->mixer) l->mixer = audio::create_mixer(true);
    if (!fill_mixer(l->mixer, bank, diags)) {
        destroy_audio_layer(l);
        *failed = true;
        return nullptr;
    }
    l->volume_changes = l->state->volume_changes;
    if (music >= 0) scene::audio_music(l->state, music, 0.0f);
    return l;
}

void destroy_audio_layer(AudioLayer* l) {
    if (!l) return;
    platform::close_audio_output(l->output);  // the audio thread stops calling mix() first
    audio::destroy_mixer(l->mixer);
    scene::destroy_audio_state(l->state);
    scene::free_sound_bank(l->bank);
    delete l;
}

scene::AudioState* audio_state(AudioLayer* l) { return l ? l->state : nullptr; }

void follow_save(AudioLayer* l, const scene::SaveData* save) {
    if (!l) return;
    l->save = save;
    for (uint32_t g = 0; g < l->bank->group_count; ++g) {
        const int f = save && l->bank->groups[g].save[0] ? scene::find_save_field(save->spec, l->bank->groups[g].save) : -1;
        const bool number = f >= 0 && save->spec->fields[f].type == scene::kSaveNumber;
        l->save_field[g] = number ? f : -1;
        l->state->group_follows_save[g] = number;
    }
    l->save_changes = save ? save->changes - 1 : 0;  // apply on the next flush
    flush_audio(l);
}

void flush_audio(AudioLayer* l) {
    if (!l) return;
    scene::AudioState* s = l->state;
    if (l->save && l->save->changes != l->save_changes) {
        for (uint32_t g = 0; g < l->bank->group_count; ++g) {
            if (l->save_field[g] < 0) continue;
            const float v = (float)l->save->values[l->save_field[g]].n;
            if (v != s->group_volume[g]) scene::set_group_volume(s, g, v);
        }
        l->save_changes = l->save->changes;
    }
    if (s->volume_changes != l->volume_changes) {
        for (uint32_t g = 0; g < l->bank->group_count; ++g) audio::set_group_volume(l->mixer, g, s->group_volume[g]);
        l->volume_changes = s->volume_changes;
    }
    for (uint32_t i = 0; i < s->queue_count; ++i) {
        const scene::AudioCommand& c = s->queue[i];
        switch (c.kind) {
            case scene::AudioCommand::kPlay: audio::play(l->mixer, c.sound, c.volume, c.pitch, c.pan); break;
            case scene::AudioCommand::kMusic: audio::play_music(l->mixer, (int)c.sound, c.fade); break;
            case scene::AudioCommand::kStopMusic: audio::play_music(l->mixer, -1, c.fade); break;
        }
    }
    s->queue_count = 0;
    audio::update(l->mixer);
}

void pause_audio(AudioLayer* l, bool paused) {
    if (l) platform::pause_audio_output(l->output, paused);
}

int64_t sound_play_count(const AudioLayer* l, const char* sound) {
    const int i = l ? scene::find_sound(l->bank, sound) : -1;
    return i < 0 ? -1 : (int64_t)l->state->play_count[i];
}

double sound_group_volume(const AudioLayer* l, const char* group) {
    const int g = l ? scene::find_sound_group(l->bank, group) : -1;
    return g < 0 ? -1.0 : (double)l->state->group_volume[g];
}

bool check_scene_audio(const scene::Scene* s, DiagList* diags) {
    const uint32_t before = diags->count;
    scene::SoundBank* bank = s->sounds[0] ? scene::load_sound_bank(s->sounds, diags) : nullptr;
    if (bank) {
        music_sound(s, bank, diags);
        for (uint32_t i = 0; i < bank->sound_count; ++i) {
            char err[400];
            if (bank->sounds[i].source == scene::Sound::kFile && !audio::probe_file(bank->sounds[i].file, err, sizeof(err))) {
                diag_add(diags, kErrSoundAsset, bank->path, bank->sounds[i].line, 1, "sound '%s': %s",
                         bank->sounds[i].name, err);
            }
        }
    }
    // Groups that follow a save field: a number field of the scene's save.
    scene::SaveSpec* spec = nullptr;
    for (uint32_t g = 0; bank && g < bank->group_count; ++g) {
        const scene::SoundGroup& group = bank->groups[g];
        if (!group.save[0]) continue;
        if (!spec && s->save[0]) {
            DiagList* ignored = new DiagList();  // check_scene_save reports the declaration itself
            spec = scene::load_save_spec(s->save, ignored);
            delete ignored;
        }
        const int f = spec ? scene::find_save_field(spec, group.save) : -1;
        if (!s->save[0]) {
            diag_add(diags, kErrSoundRef, bank->path, group.save_line, 1,
                     "group '%s' follows save field '%s', but the scene has no save = \"<file>\"", group.name, group.save);
        } else if (spec && f < 0) {
            const char* hint = scene::suggest_save_field(spec, group.save);
            diag_add(diags, kErrSoundRef, bank->path, group.save_line, 1, "group '%s': %s declares no field '%s'%s%s%s",
                     group.name, spec->path, group.save, hint ? " (did you mean '" : "", hint ? hint : "",
                     hint ? "'?)" : "");
        } else if (spec && spec->fields[f].type != scene::kSaveNumber) {
            diag_add(diags, kErrSoundRef, bank->path, group.save_line, 1,
                     "group '%s': save field '%s' is %s; a volume needs a number field (0..1)", group.name, group.save,
                     scene::save_type_name(spec->fields[f].type));
        }
    }
    scene::free_save_spec(spec);
    // Every Button.sound on the scene's screens.
    uint32_t sound_field = 0;
    while (sound_field < ui::kButtonDesc.field_count && strcmp(ui::kButtonDesc.fields[sound_field].name, "sound") != 0) {
        ++sound_field;
    }
    DiagList* ignored = new DiagList();  // check_scene_ui reports problems of the screens themselves
    for (uint32_t k = 0; k < s->ui_count + s->screen_count; ++k) {
        const char* path = k < s->ui_count ? s->ui[k] : s->screens[k - s->ui_count];
        scene::Scene* screen = scene::load_screen(path, ignored);
        for (uint32_t e = 0; screen && e < screen->entity_count; ++e) {
            const scene::Entity& node = screen->entities[e];
            const scene::ComponentData* cd = scene::find_component_data(&node, &ui::kButtonDesc);
            const auto* b = cd ? (const ui::Button*)cd->data : nullptr;
            if (!b || !b->sound[0]) continue;
            const int line = cd->field_lines[sound_field] ? cd->field_lines[sound_field] : node.line;
            if (!bank) {
                if (!s->sounds[0]) {
                    diag_add(diags, kErrSoundRef, path, line, 1,
                             "node '%s' plays sound '%s', but the scene has no sounds = \"<file>\"", node.name, b->sound);
                }
                continue;
            }
            if (scene::find_sound(bank, b->sound) >= 0) continue;
            const char* hint = scene::suggest_sound(bank, b->sound);
            diag_add(diags, kErrSoundRef, path, line, 1, "node '%s': %s has no sound '%s'%s%s%s", node.name, bank->path,
                     b->sound, hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
        }
        scene::free_scene(screen);
    }
    delete ignored;
    scene::free_sound_bank(bank);
    return diags->count == before;
}

}  // namespace zaro::app
