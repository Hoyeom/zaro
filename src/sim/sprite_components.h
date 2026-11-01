// Sprite components: which sheet and animation an entity shows, and where (P3). They are plain
// data like every component; the app loads the sheets, advances `frame` every tick from the tick
// clock (so scene tests can check it without a GPU) and hands the sprites to render.
#pragma once

#include "core/reflect.h"

#include <stdint.h>

namespace zaro::sim {

enum SpriteSpace : uint32_t { kSpriteWorld, kSpriteScreen };

struct Sprite {
    char sheet[reflect::kStringCapacity] = {};      // sprite sheet file, relative to the scene
    char animation[reflect::kStringCapacity] = {};  // empty: show `frame` as it is
    float position[3] = {0.0f, 0.0f, 0.0f};  // world: metres; screen: pixels from the top-left (z unused)
    uint32_t space = kSpriteWorld;
    float scale = 1.0f;
    uint32_t color = 0xffffffffu;
    bool flip_x = false;
    bool visible = true;
    float speed = 1.0f;   // animation playback rate
    uint32_t frame = 0;   // sheet frame shown now; the animation sets it every tick
};

// Draws the crowd's enemies as camera-facing sprites of a sheet's animation instead of cubes.
// Each enemy starts the animation at its own phase and faces the way it walks.
struct CrowdSprite {
    char sheet[reflect::kStringCapacity] = {};
    char animation[reflect::kStringCapacity] = {};
    float scale = 1.0f;
};

extern const reflect::ComponentDesc kSpriteDesc;
extern const reflect::ComponentDesc kCrowdSpriteDesc;

void register_sprite_components();

}  // namespace zaro::sim
