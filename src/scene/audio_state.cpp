#include "scene/audio_state.h"

#include <stdlib.h>

namespace zaro::scene {

AudioState* create_audio_state(const SoundBank* bank) {
    AudioState* s = (AudioState*)calloc(1, sizeof(AudioState));
    s->bank = bank;
    s->music = -1;
    for (uint32_t g = 0; g < bank->group_count; ++g) s->group_volume[g] = bank->groups[g].volume;
    return s;
}

void destroy_audio_state(AudioState* s) { free(s); }

namespace {

AudioCommand* push(AudioState* s) {
    if (s->queue_count == kMaxAudioCommands) return nullptr;
    AudioCommand* c = &s->queue[s->queue_count++];
    *c = AudioCommand{};
    return c;
}

}  // namespace

void audio_play(AudioState* s, uint32_t sound, float volume, float pitch, float pan) {
    if (sound >= s->bank->sound_count) return;
    ++s->play_count[sound];
    AudioCommand* c = push(s);
    if (!c) {
        ++s->dropped;
        return;
    }
    c->kind = AudioCommand::kPlay;
    c->sound = sound;
    c->volume = volume;
    c->pitch = pitch;
    c->pan = pan;
}

void audio_music(AudioState* s, int sound, float fade) {
    if (sound >= (int)s->bank->sound_count) return;
    if (sound == s->music) return;  // asking for the track that plays already keeps it going
    s->music = sound < 0 ? -1 : sound;
    if (sound >= 0) ++s->play_count[sound];
    AudioCommand* c = push(s);
    if (!c) {
        // Keep the newest music request even when plays filled the queue.
        c = &s->queue[s->queue_count - 1];
        ++s->dropped;
    }
    *c = AudioCommand{};
    c->kind = sound < 0 ? AudioCommand::kStopMusic : AudioCommand::kMusic;
    c->sound = sound < 0 ? 0 : (uint32_t)sound;
    c->fade = fade;
}

void set_group_volume(AudioState* s, uint32_t group, float volume) {
    if (group >= s->bank->group_count) return;
    s->group_volume[group] = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    ++s->volume_changes;
}

}  // namespace zaro::scene
