// GPU instance data built from the simulation state, shared by the Direct3D
// (Windows) and Metal (macOS) renderers. Pure C++: the layouts below are what
// the shaders on both platforms read, so they must not change on one side only.
#pragma once

#include <vector>

namespace sim {
class World;
}

namespace render {

// a = (centre.x, centre.y, radius, alpha), b = (angle, squash, sprite, gain)
struct FlakeInstance {
    float x, y, radius, alpha;
    float angle, squash, sprite, gain;
};

// (centre.x, centre.y, radius, rollAngle)
struct BallInstance {
    float x, y, radius, angle;
};

// a = (centre.x, centre.y, halfLength, halfWidth), b = (angle, r, g, b),
// c = (alpha, shine, type, 0). type: 0 capsule, 1 triangle, 2 trapezoid.
struct ShapeInstance {
    float cx, cy, halfLength, halfWidth;
    float angle, r, g, b;
    float alpha, shine, type, pad;
};

// One trapezoid per height-map cell: a = (x0, x1, baseY, unused), b = (heightLeft, heightRight)
struct PileInstance {
    float x0, x1, base, unused;
    float hLeft, hRight;
};

static_assert(sizeof(FlakeInstance) == 32);
static_assert(sizeof(BallInstance) == 16);
static_assert(sizeof(ShapeInstance) == 48);
static_assert(sizeof(PileInstance) == 24);

// Everything one frame draws, in draw order: piles, balls, shapes, flakes.
struct FrameInstances {
    std::vector<PileInstance> piles;
    std::vector<BallInstance> balls;
    std::vector<ShapeInstance> shapes;
    std::vector<FlakeInstance> flakes;

    // Refills all lists from the world (keeps their capacity between frames).
    void build(const sim::World& world);
};

}  // namespace render
