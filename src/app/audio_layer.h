// The scene's sounds (D-032): its sound file, the requests that scripts, buttons and `[scene] music`
// queue in scene::AudioState, the mixer, and the platform's audio output. Requests go to the mixer
// after each tick and each frame's UI; nothing flows back, so audio never changes a run (D-005).
#pragma once

#include "core/diag.h"

#include <stdint.h>

namespace zaro::scene {
struct AudioState;
struct SaveData;
struct Scene;
}  // namespace zaro::scene

namespace zaro::app {

struct AudioLayer;

// Loads the scene's sound file and decodes its sounds, and queues its music. With null_output
// (headless runs, scene tests) or when the machine has no output device, the mixer only counts
// plays. Returns nullptr when the scene has no sounds, or after adding diagnostics (*failed is
// then true).
AudioLayer* create_audio_layer(const scene::Scene* scene, bool null_output, DiagList* diags, bool* failed);
// Closes the output first, then frees the mixer.
void destroy_audio_layer(AudioLayer* layer);

// What `audio.*` and buttons write to (pass it in script::ScriptDesc and to set_audio).
scene::AudioState* audio_state(AudioLayer* layer);
// Hands the queued requests and volume changes to the mixer and empties the queue.
void flush_audio(AudioLayer* layer);
// Groups with `save = "<field>"` follow that field of the game's save from now on (nullptr: none).
void follow_save(AudioLayer* layer, const scene::SaveData* save);
// Mobile background: stops the output and resumes it.
void pause_audio(AudioLayer* layer, bool paused);
// How many times a sound was asked to play during the run (scene tests); -1 for no such sound.
int64_t sound_play_count(const AudioLayer* layer, const char* sound);
// A group's volume as last set; -1 for no such group.
double sound_group_volume(const AudioLayer* layer, const char* group);

// `zaro check`: the scene's sound file, that every sound decodes, that [scene] music names one of its
// sounds, that every Button.sound on the scene's screens does, and that a group's `save` names a
// number field of the scene's save. Returns false if anything was
// reported. A scene without sounds whose buttons name one is reported too.
bool check_scene_audio(const scene::Scene* scene, DiagList* diags);

}  // namespace zaro::app
