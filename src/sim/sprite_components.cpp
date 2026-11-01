#include "sim/sprite_components.h"

namespace zaro::sim {

namespace {

const char* const kSpaces[] = {"world", "screen", nullptr};

const reflect::FieldDesc kSpriteFields[] = {
    ZARO_FIELD(Sprite, sheet, kString, "Sprite sheet file (.toml), relative to the scene"),
    ZARO_FIELD(Sprite, animation, kString, "Animation of the sheet to play; empty shows `frame`"),
    ZARO_FIELD(Sprite, position, kVec3, "world: position in metres; screen: x, y in pixels from the top-left"),
    ZARO_FIELD_ENUM(Sprite, space, "world faces the camera in the 3D view; screen is drawn over it in pixels",
                    kSpaces),
    ZARO_FIELD_RANGE(Sprite, scale, kF32, "Size multiplier on the frame's size", 0.0, 1000.0),
    ZARO_FIELD(Sprite, color, kColor, "Multiplies the sprite's colours"),
    ZARO_FIELD(Sprite, flip_x, kBool, "Mirror the sprite horizontally"),
    ZARO_FIELD(Sprite, visible, kBool, "Draw the sprite"),
    ZARO_FIELD_RANGE(Sprite, speed, kF32, "Animation playback rate; 1 plays at the sheet's fps", 0.0, 100.0),
    ZARO_FIELD_RANGE(Sprite, frame, kU32, "Sheet frame shown; set every tick while an animation plays", 0, 1023),
};

}  // namespace

ZARO_COMPONENT(Sprite, "A sprite from a sheet, in the 3D view or on the screen, optionally animated", kSpriteFields);

void register_sprite_components() {
    reflect::register_component(&kSpriteDesc);
}

}  // namespace zaro::sim
