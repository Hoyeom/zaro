// Shape components: glowing geometric shapes for the neon look (P6, D-029). Plain data like every
// component; the app advances `flash` on the tick clock (so scene tests can check it without a GPU)
// and hands the shapes to render::ShapeRenderer. Nothing here feeds the simulation (D-005).
#pragma once

#include "core/reflect.h"

#include <stdint.h>

namespace zaro::sim {

enum ShapeKindChoice : uint32_t { kShapeKindRect, kShapeKindCircle, kShapeKindRing, kShapeKindPolygon, kShapeKindLine };
enum ShapeBlend : uint32_t { kShapeBlendAdd, kShapeBlendAlpha };
enum ShapeFollow : uint32_t { kShapeFollowNone, kShapeFollowPlayer };

// One shape. In the world it lies flat on the ground plane (y is its height); on the screen it is
// in pixels from the top-left. Following the player, `position` is an offset from it.
struct Shape {
    uint32_t kind = kShapeKindRing;
    float position[3] = {0.0f, 0.0f, 0.0f};
    uint32_t space = 0;  // SpriteSpace: world or screen
    uint32_t follow = kShapeFollowNone;
    float size[2] = {1.0f, 1.0f};
    float rotation = 0.0f;  // degrees
    float spin = 0.0f;      // degrees per second, on the tick clock
    uint32_t sides = 6;
    float thickness = 0.0f;
    uint32_t color = 0xffffffffu;
    float intensity = 1.0f;
    float glow = 0.0f;
    float glow_strength = 0.6f;
    uint32_t blend = kShapeBlendAdd;
    bool visible = true;
    float trail = 0.0f;        // seconds of trail behind a moving shape
    float trail_width = 0.0f;  // 0: a third of the shape's size
    float flash = 0.0f;        // 0..1: draws the shape white; falls by flash_decay per second
    float flash_decay = 4.0f;
};

// Draws the crowd's enemies as glowing shapes instead of cubes, each turned the way it walks.
struct CrowdShape {
    uint32_t kind = kShapeKindPolygon;
    uint32_t sides = 3;
    float size = 0.8f;
    float thickness = 0.08f;
    uint32_t color = 0xff4d2bffu;  // #ff2b4d
    float intensity = 2.0f;
    float glow = 0.25f;
    float glow_strength = 0.5f;
    float hue_jitter = 0.08f;  // 0..1: each enemy's hue shifts by up to this much
    uint32_t blend = kShapeBlendAdd;
};

enum ArenaPattern : uint32_t { kArenaGrid, kArenaLines, kArenaDots, kArenaPlain };

// The arena's look: a dark ground with a pattern and tinted obstacles, instead of the grey
// checkerboard.
struct ArenaLook {
    uint32_t ground = 0xff0a0605u;  // #05060a
    uint32_t grid = 0xff663a1au;    // #1a3a66
    float grid_intensity = 1.0f;
    float grid_spacing = 4.0f;
    uint32_t obstacle = 0xff402820u;  // #202840
    float obstacle_intensity = 1.0f;
    uint32_t sky = 0xff0a0605u;  // clear colour and distance fog
    uint32_t pattern = kArenaGrid;  // D-039
};

extern const reflect::ComponentDesc kShapeDesc;
extern const reflect::ComponentDesc kCrowdShapeDesc;
extern const reflect::ComponentDesc kArenaLookDesc;

void register_shape_components();

}  // namespace zaro::sim
