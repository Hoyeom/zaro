#include "sim/fx_components.h"

namespace zaro::sim {

namespace {

const char* const kHdrFormats[] = {"auto", "rgba16f", "rg11b10f", nullptr};

const reflect::FieldDesc kBloomFields[] = {
    ZARO_FIELD(Bloom, enabled, kBool, "Draw the glow; false keeps the HDR frame and tonemap without it"),
    ZARO_FIELD_RANGE(Bloom, threshold, kF32, "Linear brightness where glow starts (colour x intensity above it glows)",
                     0.0, 100.0),
    ZARO_FIELD_RANGE(Bloom, knee, kF32, "0: hard cut at the threshold; 1: glow fades in from black", 0.0, 1.0),
    ZARO_FIELD_RANGE(Bloom, intensity, kF32, "Strength of the glow added to the frame", 0.0, 10.0),
    ZARO_FIELD_RANGE(Bloom, radius, kF32, "0..1: how far the glow spreads", 0.0, 1.0),
    ZARO_FIELD_RANGE(Bloom, levels, kU32, "Half-size levels of the glow; fewer is cheaper and tighter", 1, 8),
    ZARO_FIELD_RANGE(Bloom, exposure, kF32, "Multiplies the scene's light before tonemapping", 0.0, 16.0),
    ZARO_FIELD_ENUM(Bloom, format,
                    "HDR frame format: auto picks rg11b10f where the GPU renders it, else rgba16f (D-030)",
                    kHdrFormats),
};

const reflect::FieldDesc kScreenFxFields[] = {
    ZARO_FIELD_RANGE(ScreenFx, shake, kF32, "Trauma 0..1: the camera shakes by its square; add to it on a hit", 0.0,
                     1.0),
    ZARO_FIELD_RANGE(ScreenFx, shake_decay, kF32, "How much shake falls per second, on the tick clock", 0.0, 100.0),
    ZARO_FIELD_RANGE(ScreenFx, shake_strength, kF32, "Camera offset in metres at full shake", 0.0, 100.0),
    ZARO_FIELD_RANGE(ScreenFx, flash, kF32, "0..1 of flash_color over the frame; add to it on a hit", 0.0, 1.0),
    ZARO_FIELD_RANGE(ScreenFx, flash_decay, kF32, "How much flash falls per second, on the tick clock", 0.0, 100.0),
    ZARO_FIELD(ScreenFx, flash_color, kColor, "Colour of the full-screen flash"),
};

const reflect::FieldDesc kCrowdFxFields[] = {
    ZARO_FIELD_RANGE(CrowdFx, particles, kU32, "Sparks where an enemy reached the player; 0: none", 0, 1000),
    ZARO_FIELD(CrowdFx, color, kColor, "Colour of the sparks"),
    ZARO_FIELD_RANGE(CrowdFx, intensity, kF32, "Multiplies the spark colour; above 1 it glows under Bloom", 0.0,
                     1000.0),
    ZARO_FIELD_RANGE(CrowdFx, speed, kF32, "Fastest spark speed in metres per second (the slowest is 40% of it)",
                     0.0, 1000.0),
    ZARO_FIELD_RANGE(CrowdFx, life, kF32, "Seconds a spark lives at most", 0.01, 10.0),
    ZARO_FIELD_RANGE(CrowdFx, size, kF32, "Spark width in metres", 0.001, 10.0),
    ZARO_FIELD_RANGE(CrowdFx, shake, kF32, "Added to ScreenFx.shake per hit", 0.0, 1.0),
    ZARO_FIELD_RANGE(CrowdFx, flash, kF32, "Added to ScreenFx.flash per hit", 0.0, 1.0),
    ZARO_FIELD_RANGE(CrowdFx, player_flash, kF32, "Shape.flash set on shapes following the player per hit", 0.0, 1.0),
};

const char* const kEmitterFollows[] = {"none", "player", nullptr};

const reflect::FieldDesc kParticleEmitterFields[] = {
    ZARO_FIELD(ParticleEmitter, position, kVec3, "World position in metres; an offset from the player when following"),
    ZARO_FIELD_ENUM(ParticleEmitter, follow, "player: emits at the player's position plus `position`",
                    kEmitterFollows),
    ZARO_FIELD(ParticleEmitter, enabled, kBool, "Emit at `rate`; a burst is emitted either way"),
    ZARO_FIELD_RANGE(ParticleEmitter, rate, kF32, "Sparks per second, continuously", 0.0, 100000.0),
    ZARO_FIELD_RANGE(ParticleEmitter, burst, kU32, "Set above 0 to emit that many at once; it returns to 0", 0,
                     100000),
    ZARO_FIELD(ParticleEmitter, color, kColor, "Colour of the sparks"),
    ZARO_FIELD_RANGE(ParticleEmitter, intensity, kF32, "Multiplies the colour; above 1 it glows under Bloom", 0.0,
                     1000.0),
    ZARO_FIELD_RANGE(ParticleEmitter, speed, kF32, "Fastest speed in metres per second (the slowest is 40% of it)",
                     0.0, 1000.0),
    ZARO_FIELD_RANGE(ParticleEmitter, life, kF32, "Seconds a spark lives at most", 0.01, 10.0),
    ZARO_FIELD_RANGE(ParticleEmitter, size, kF32, "Spark width in metres", 0.001, 10.0),
    ZARO_FIELD_RANGE(ParticleEmitter, drag, kF32, "Speed lost per second (exponential)", 0.0, 100.0),
    ZARO_FIELD(ParticleEmitter, direction, kF32, "Degrees; 0 is +x and +90 is +z (down on the screen), as Transform"),
    ZARO_FIELD_RANGE(ParticleEmitter, spread, kF32, "Degrees around the direction; 360: every way", 0.0, 360.0),
};

}  // namespace

ZARO_COMPONENT(ScreenFx, "Screen shake and full-screen flash that fall on the tick clock (one per scene)",
               kScreenFxFields);
ZARO_COMPONENT(CrowdFx, "Sparks, shake and flash when an enemy of the crowd reaches the player", kCrowdFxFields);
ZARO_COMPONENT(ParticleEmitter, "GPU sparks from a point: a steady rate, or a burst a script fires",
               kParticleEmitterFields);
ZARO_COMPONENT(Bloom, "Renders the 3D view in HDR: colours brighter than the threshold glow (D-030)", kBloomFields);

void register_fx_components() {
    reflect::register_component(&kBloomDesc);
    reflect::register_component(&kScreenFxDesc);
    reflect::register_component(&kCrowdFxDesc);
    reflect::register_component(&kParticleEmitterDesc);
}

}  // namespace zaro::sim
