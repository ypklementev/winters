// Solid vector shapes drawn on top of the snow: garland lights, the
// windshield wiper and snowman decorations (bucket hat, carrot nose, eyes, smile, buttons, twig
// arms). Pure C++ so the layout can be previewed and tested off Windows.
#pragma once

#include <vector>

namespace sim {
class World;
}

namespace shapes {

// kGlow: soft radial halo of radius halfLength, lighter-than-alpha (light
// from a garland bulb); halfWidth is ignored.
enum Type { kCapsule = 0, kTriangle = 1, kTrapezoid = 2, kGlow = 3 };

struct Shape {
    float cx, cy;                 // centre, px
    float halfLength, halfWidth;  // along / across the shape's axis
    float angle;                  // rotation of the axis, rad (screen space)
    float r, g, b;
    float alpha;
    float shine;
    int type;  // Type
};

std::vector<Shape> Build(const sim::World& world);

}  // namespace shapes
