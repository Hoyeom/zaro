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
    float direction = 0.0f;  // degrees; +x turns toward +z
    float spread = 360.0f;   // degrees around the direction
};

enum CameraFollow : uint32_t { kCameraFollowPlayer, kCameraFollowNone };
enum CameraFovAxis : uint32_t { kFovVertical, kFovHorizontal, kFovShorter };

// The 3D view's camera, one per scene (the first counts; D-035). It looks down at a point from
// `height` metres above it, tilted `pitch` degrees below the horizon, from the +z side (the screen's
// up is -z). Without one the camera is 30 m above and 24 m behind the player with a 50-degree
// vertical field of view, which these defaults match.
struct Camera {
    float height = 30.0f;  // metres above the point it looks at (times `zaro run --zoom`)
    float pitch = 51.34f;  // degrees below the horizon; 90 looks straight down
    float fov = 49.85f;    // degrees across fov_axis
    uint32_t fov_axis = kFovVertical;
    uint32_t follow = kCameraFollowPlayer;
    float target[3] = {};  // the point it looks at when follow = none
    float offset[3] = {};  // added to the point it looks at
    float lag = 0.0f;      // seconds to close most of the gap to a moving point; 0: none
    float fog = 60.0f;     // metres from the camera where distance fog starts
};

extern const reflect::ComponentDesc kBloomDesc;
extern const reflect::ComponentDesc kScreenFxDesc;
extern const reflect::ComponentDesc kCrowdFxDesc;
extern const reflect::ComponentDesc kParticleEmitterDesc;
extern const reflect::ComponentDesc kCameraDesc;

void register_fx_components();

}  // namespace zaro::sim
