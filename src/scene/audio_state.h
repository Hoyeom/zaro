// What a run asks its audio to do (D-032): plays, the music track and group volumes, from scripts
// (`audio.*`), UI buttons and the scene's `music`. The script module and the UI layer write it; the
// app hands the queue to the mixer after each tick and frame and empties it. Nothing here is ever
// read back from the mixer, so scripts see only what they asked for and runs stay deterministic
// (D-005). Scene tests read the play counts.
#pragma once

#include "scene/sound_bank.h"

#include <stdint.h>

namespace zaro::scene {

constexpr uint32_t kMaxAudioCommands = 1024;  // per drain; more plays in one tick are dropped

struct AudioCommand {
    enum Kind : uint8_t { kPlay, kMusic, kStopMusic };
    Kind kind;
    uint32_t sound;  // index into the bank (kPlay, kMusic)
    float volume;    // kPlay: multiplies the sound's volume
    float pitch;     // kPlay: 0 picks one in the sound's range
    float pan;       // kPlay: -1 left .. 1 right
    float fade;      // kMusic, kStopMusic: seconds
};

struct AudioState {
    const SoundBank* bank;
    float group_volume[kMaxSoundGroups];  // as last set; starts at the file's volumes
    bool group_follows_save[kMaxSoundGroups];  // set from a save field (D-033), not by audio.volume
    uint32_t volume_changes;              // bumped by every set_group_volume
    int music;                            // the track asked for last, -1 for none
    uint32_t play_count[kMaxSounds];      // plays asked for since the run started
    uint32_t dropped;                     // plays that did not fit in the queue
    AudioCommand queue[kMaxAudioCommands];
    uint32_t queue_count;
};

AudioState* create_audio_state(const SoundBank* bank);
void destroy_audio_state(AudioState* state);

void audio_play(AudioState* state, uint32_t sound, float volume, float pitch, float pan);
void audio_music(AudioState* state, int sound, float fade);  // -1 stops the music
void set_group_volume(AudioState* state, uint32_t group, float volume);

}  // namespace zaro::scene
