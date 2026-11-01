#include "sim/shape_components.h"

namespace zaro::sim {

namespace {

const char* const kShapeKinds[] = {"rect", "circle", "ring", "polygon", "line", nullptr};
const char* const kShapeBlends[] = {"add", "alpha", nullptr};
const char* const kShapeFollows[] = {"none", "player", nullptr};
const char* const kShapeSpaces[] = {"world", "screen", nullptr};

const reflect::FieldDesc kShapeFields[] = {
    ZARO_FIELD_ENUM(Shape, kind, "rect, circle, ring (an outlined circle), polygon (regular, `sides`) or line", kShapeKinds),
    ZARO_FIELD(Shape, position, kVec3,
               "world: x, height, z in metres (flat on the ground); screen: x, y pixels from the top-left; "
               "an offset from the player when following"),
    ZARO_FIELD_ENUM(Shape, space, "world lies on the ground of the 3D view; screen is drawn over it in pixels",
                    kShapeSpaces),
    ZARO_FIELD_ENUM(Shape, follow, "player: drawn at the player's position plus `position` (world only)", kShapeFollows),
    ZARO_FIELD(Shape, size, kVec2,
               "rect: width, height; circle, ring, polygon: diameter in x; line: length in x (metres or pixels)"),
    ZARO_FIELD(Shape, rotation, kF32, "Degrees; a polygon's first corner and a line point along +x at 0"),
    ZARO_FIELD(Shape, spin, kF32, "Degrees per second added to rotation, on the tick clock"),
    ZARO_FIELD_RANGE(Shape, sides, kU32, "Corners of a polygon", 3, 64),
    ZARO_FIELD_RANGE(Shape, thickness, kF32,
                     "Outline width; 0 fills (a ring then uses a tenth of its size); a line's width", 0.0, 10000.0),
    ZARO_FIELD(Shape, color, kColor, "Colour; its alpha is the opacity"),
    ZARO_FIELD_RANGE(Shape, intensity, kF32, "Multiplies the colour; above 1 it glows under Bloom", 0.0, 1000.0),
    ZARO_FIELD_RANGE(Shape, glow, kF32, "Width of the halo beyond the edge, in the shape's units; 0: none", 0.0,
                     10000.0),
    ZARO_FIELD_RANGE(Shape, glow_strength, kF32, "Opacity of the halo at the edge", 0.0, 1.0),
    ZARO_FIELD_ENUM(Shape, blend, "add: adds light (neon); alpha: covers what is behind", kShapeBlends),
    ZARO_FIELD(Shape, visible, kBool, "Draw the shape"),
    ZARO_FIELD_RANGE(Shape, trail, kF32, "Seconds of fading trail behind the shape while it moves; 0: none", 0.0, 2.0),
    ZARO_FIELD_RANGE(Shape, trail_width, kF32, "Width of the trail at the shape; 0: a third of the shape's size", 0.0, 10000.0),
    ZARO_FIELD_RANGE(Shape, flash, kF32, "Hit flash: 1 draws the shape white, then it fades", 0.0, 1.0),
    ZARO_FIELD_RANGE(Shape, flash_decay, kF32, "How much flash falls per second, on the tick clock", 0.0, 1000.0),
};

const reflect::FieldDesc kCrowdShapeFields[] = {
    ZARO_FIELD_ENUM(CrowdShape, kind, "Shape of every enemy", kShapeKinds),
    ZARO_FIELD_RANGE(CrowdShape, sides, kU32, "Corners of a polygon; the first points the way the enemy walks", 3, 64),
    ZARO_FIELD_RANGE(CrowdShape, size, kF32, "Diameter (rect: side) in metres", 0.01, 100.0),
    ZARO_FIELD_RANGE(CrowdShape, thickness, kF32, "Outline width in metres; 0 fills", 0.0, 100.0),
    ZARO_FIELD(CrowdShape, color, kColor, "Colour; its alpha is the opacity"),
    ZARO_FIELD_RANGE(CrowdShape, intensity, kF32, "Multiplies the colour; above 1 it glows under Bloom", 0.0, 1000.0),
    ZARO_FIELD_RANGE(CrowdShape, glow, kF32, "Width of the halo in metres; 0: none", 0.0, 100.0),
    ZARO_FIELD_RANGE(CrowdShape, glow_strength, kF32, "Opacity of the halo at the edge", 0.0, 1.0),
    ZARO_FIELD_RANGE(CrowdShape, hue_jitter, kF32, "How far each enemy's hue may differ (0..1 of the colour wheel)",
                     0.0, 1.0),
    ZARO_FIELD_ENUM(CrowdShape, blend, "add: adds light (neon); alpha: covers what is behind", kShapeBlends),
};

const reflect::FieldDesc kArenaLookFields[] = {
    ZARO_FIELD(ArenaLook, ground, kColor, "Colour of the ground"),
    ZARO_FIELD(ArenaLook, grid, kColor, "Colour of the grid lines on the ground"),
    ZARO_FIELD_RANGE(ArenaLook, grid_intensity, kF32, "Multiplies the grid colour; above 1 it glows under Bloom",
                     0.0, 1000.0),
    ZARO_FIELD_RANGE(ArenaLook, grid_spacing, kF32, "Metres between grid lines", 0.25, 256.0),
    ZARO_FIELD(ArenaLook, obstacle, kColor, "Colour of the obstacles"),
    ZARO_FIELD_RANGE(ArenaLook, obstacle_intensity, kF32, "Multiplies the obstacle colour", 0.0, 1000.0),
    ZARO_FIELD(ArenaLook, sky, kColor, "Background and distance fog colour"),
};

}  // namespace

ZARO_VISUAL_COMPONENT(Shape, "A glowing shape (rect, circle, ring, polygon, line) on the ground or on the screen",
               kShapeFields);
ZARO_VISUAL_COMPONENT(CrowdShape, "Draws the crowd's enemies as glowing shapes instead of cubes", kCrowdShapeFields);
ZARO_VISUAL_COMPONENT(ArenaLook, "The arena's colours: ground, glowing grid, obstacles and background", kArenaLookFields);

void register_shape_components() {
    reflect::register_component(&kShapeDesc);
    reflect::register_component(&kCrowdShapeDesc);
    reflect::register_component(&kArenaLookDesc);
}

}  // namespace zaro::sim
