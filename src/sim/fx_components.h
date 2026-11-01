// Visual effect components (P6, D-029): bloom over an HDR frame, screen shake and flash, and GPU
// particles for hits and other bursts. They are plain data like every component; the app advances
// the decaying values on the tick clock (so scene tests can check them without a GPU) and hands the
// rest to render. Nothing here feeds the simulation or its state hash (D-005).
#pragma once

#include "core/reflect.h"

#include <stdint.h>

namespace zaro::sim {

enum HdrFormat : uint32_t { kHdrAuto, kHdrRgba16f, kHdrRg11b10f };

// With a Bloom component anywhere in the scene, the 3D view renders into an HDR frame: colours
// brighter than 1 glow, then the frame is tonemapped. The first one in the scene is used.
struct Bloom {
    bool enabled = true;     // false keeps the HDR frame and tonemap but draws no glow
    float threshold = 1.0f;  // linear brightness where glow starts
    float knee = 0.5f;
    float intensity = 0.8f;
    float radius = 0.7f;
    uint32_t levels = 5;
    float exposure = 1.0f;
    uint32_t format = kHdrAuto;
};

// Screen shake and flash, one per scene (the first counts). Scripts and CrowdFx add to `shake` and
// `flash`; both fall on the tick clock.
struct ScreenFx {
    float shake = 0.0f;           // trauma 0..1; the camera moves by its square
    float shake_decay = 1.5f;     // per second
    float shake_strength = 0.6f;  // metres at full trauma
    float flash = 0.0f;           // 0..1 of flash_color over the frame
    float flash_decay = 4.0f;     // per second
    uint32_t flash_color = 0xffffffffu;
};

// What happens when an enemy of the crowd reaches the player (a hit: it vanishes and respawns at the
// edge): a burst of sparks where it was, shake, flash, and a hit flash on shapes following the
// player. On the crowd's spawner entity.
struct CrowdFx {
    uint32_t particles = 12;
    uint32_t color = 0xff4d2bffu;  // #ff2b4d
    float intensity = 3.0f;
    float speed = 6.0f;
    float life = 0.5f;
    float size = 0.12f;
    float shake = 0.08f;
    float flash = 0.0f;
    float player_flash = 1.0f;
};

enum EmitterFollow : uint32_t { kEmitterFollowNone, kEmitterFollowPlayer };

// Sparks from a point: `rate` per second, and `burst` at once (the app emits them and sets burst
// back to 0 on the next tick, so a script fires a burst with zaro.set).
struct ParticleEmitter {
    float position[3] = {0.0f, 0.3f, 0.0f};
    uint32_t follow = kEmitterFollowNone;
    bool enabled = true;
    float rate = 0.0f;
    uint32_t burst = 0;
    uint32_t color = 0xffffffffu;
    float intensity = 2.0f;
    float speed = 4.0f;
    float life = 0.6f;
    float size = 0.1f;
    float drag = 2.0f;
    float direction = 0.0f;  // degrees from +x toward -z
    float spread = 360.0f;   // degrees around the direction
};

extern const reflect::ComponentDesc kBloomDesc;
extern const reflect::ComponentDesc kScreenFxDesc;
extern const reflect::ComponentDesc kCrowdFxDesc;
extern const reflect::ComponentDesc kParticleEmitterDesc;

void register_fx_components();

}  // namespace zaro::sim
