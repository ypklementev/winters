#include "render/shapes.h"

#include <algorithm>
#include <cmath>

#include "sim/world.h"

namespace shapes {

namespace {
// Positional constructor matching the GPU instance layout (last field unused).
Shape mk(float cx, float cy, float hl, float hw, float angle, float r, float g, float b, float alpha, float shine,
         float type, float /*pad*/) {
    return {cx, cy, hl, hw, angle, r, g, b, alpha, shine, static_cast<int>(type)};
}
}  // namespace

std::vector<Shape> Build(const sim::World& world) {
    std::vector<Shape> out;
    // Snowman decorations pop in when the third ball settles: bucket hat,
    // carrot nose, button eyes, coal smile, buttons and twig arms.
    for (const sim::Snowman& m : world.snowmen()) {
        const sim::Snowball& head = world.snowballs()[m.head];
        const sim::Snowball& mid = world.snowballs()[m.middle];
        const float age = world.time() - head.decoratedAt;
        float k = std::clamp(age / 0.5f, 0.0f, 1.0f);
        k = k * k * (3.0f - 2.0f * k);
        if (k <= 0.0f) continue;
        const float rh = head.radius, rm = mid.radius;
        const sim::Vec2 H = head.pos, M = mid.pos;
        auto disc = [&](sim::Vec2 c, float r, float g, float b, float rad) {
            out.push_back(mk(c.x, c.y, rad, rad, 0.0f, r, g, b, 1.0f, 0.25f, 0.0f, 0.0f));
        };
        // Twig arms first (behind the middle ball's edge).
        for (float side : {-1.0f, 1.0f}) {
            const sim::Vec2 from{M.x + side * rm * 0.75f, M.y - rm * 0.15f};
            const float len = rm * 0.9f * k;
            const float ang = side > 0.0f ? -0.45f : 3.14159f + 0.45f;
            out.push_back(mk(from.x + std::cos(ang) * len * 0.5f, from.y + std::sin(ang) * len * 0.5f, len * 0.5f,
                               std::max(1.2f, rm * 0.045f), ang, 0.36f, 0.23f, 0.12f, 1.0f, 0.1f, 0.0f, 0.0f));
        }
        for (float f : {-0.45f, 0.0f, 0.45f}) disc({M.x, M.y + rm * f}, 0.08f, 0.08f, 0.09f, rm * 0.09f * k);
        // Bucket hat, slightly tilted, with a dark rim.
        const float hb = rh * 0.42f * k, bw = rh * 0.62f * k;
        const float tilt = -0.18f;
        const sim::Vec2 bucketBottom{H.x + rh * 0.1f, H.y - rh * 0.62f};
        const sim::Vec2 up{std::sin(tilt), -std::cos(tilt)};
        const sim::Vec2 bc = bucketBottom + up * hb;
        out.push_back(mk(bc.x, bc.y, bw, hb, tilt, 0.42f, 0.47f, 0.55f, 1.0f, 0.35f, 2.0f, 0.0f));
        out.push_back(mk(bucketBottom.x, bucketBottom.y, bw * 1.06f, std::max(1.0f, rh * 0.06f), tilt, 0.25f,
                           0.28f, 0.33f, 1.0f, 0.3f, 0.0f, 0.0f));
        // Eyes and a coal smile.
        for (float side : {-1.0f, 1.0f}) disc({H.x + side * rh * 0.32f, H.y - rh * 0.18f}, 0.06f, 0.06f, 0.07f, rh * 0.1f * k);
        for (int d = 0; d < 5; ++d) {
            const float u = (static_cast<float>(d) - 2.0f) / 2.0f;
            disc({H.x + rh * 0.17f * static_cast<float>(d - 2), H.y + rh * (0.32f + 0.1f * (1.0f - u * u))}, 0.1f, 0.1f,
                0.11f, rh * 0.055f * k);
        }
        // Carrot nose pointing right, slightly down.
        const float nl = rh * 0.38f * k;
        const float na = 0.12f;
        const sim::Vec2 nb{H.x + rh * 0.02f, H.y + rh * 0.06f};
        out.push_back(mk(nb.x + std::cos(na) * nl, nb.y + std::sin(na) * nl, nl, rh * 0.11f * k, na, 0.98f, 0.52f,
                           0.12f, 1.0f, 0.0f, 1.0f, 0.0f));
    }

    // The wiper: arm, blade holder, rubber blade and the pivot cap.
    const sim::Wiper& wp = world.wiper();
    if (wp.enabled()) {
        const float sc = wp.scale();
        const sim::Vec2 pv = wp.pivot();
        const sim::Vec2 tip = wp.tip();
        const sim::Vec2 bladeStart = wp.bladeStart();
        const float screenAngle = -wp.angle();  // direction (cos a, -sin a)
        auto capsule = [&](sim::Vec2 a, sim::Vec2 b, float halfWidth, float r, float g, float bl, float shine) {
            const float half = sim::length(b - a) * 0.5f + halfWidth;
            out.push_back(mk((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, half, halfWidth, screenAngle, r, g, bl,
                               1.0f, shine, 0.0f, 0.0f));
        };
        // Blade holder sits slightly offset from the arm, like on a car.
        const sim::Vec2 n = sim::Vec2{-std::sin(wp.angle()), -std::cos(wp.angle())} * (3.0f * sc);
        capsule(bladeStart + n, tip + n, 2.0f * sc, 0.16f, 0.17f, 0.19f, 0.35f);   // holder
        capsule(bladeStart, tip, wp.bladeHalfWidth(), 0.05f, 0.05f, 0.06f, 0.18f);  // rubber
        capsule(pv, tip, 2.6f * sc, 0.13f, 0.14f, 0.16f, 0.45f);                    // arm
        const float pr = wp.pivotRadius();
        out.push_back(mk(pv.x, pv.y, pr * 0.75f, pr * 0.75f, 0.0f, 0.2f, 0.21f, 0.24f, 1.0f, 0.5f, 0.0f, 0.0f));
        out.push_back(mk(pv.x, pv.y, pr * 0.32f, pr * 0.32f, 0.0f, 0.08f, 0.08f, 0.09f, 1.0f, 0.2f, 0.0f, 0.0f));
    }
    return out;
}

}  // namespace shapes
