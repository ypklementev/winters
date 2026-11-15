#include "render/shapes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "sim/world.h"

namespace shapes {

namespace {
// Positional constructor matching the GPU instance layout (last field unused).
Shape mk(float cx, float cy, float hl, float hw, float angle, float r, float g, float b, float alpha, float shine,
         float type, float /*pad*/) {
    return {cx, cy, hl, hw, angle, r, g, b, alpha, shine, static_cast<int>(type)};
}

struct Rgb {
    float r, g, b;
};

// Capsule from a to b (rounded ends included).
void Capsule(std::vector<Shape>& out, sim::Vec2 a, sim::Vec2 b, float halfWidth, Rgb c, float shine, float alpha) {
    const sim::Vec2 d = b - a;
    const float half = sim::length(d) * 0.5f + halfWidth;
    out.push_back(mk((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, half, halfWidth, std::atan2(d.y, d.x), c.r, c.g, c.b,
                     alpha, shine, kCapsule, 0.0f));
}

float Hash01(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<float>(x >> 40) / static_cast<float>(1ull << 24);
}

Rgb BulbColor(sim::GarlandColors colors, std::uint32_t index) {
    static constexpr Rgb kMulti[] = {
        {1.0f, 0.16f, 0.12f}, {0.16f, 0.86f, 0.30f}, {1.0f, 0.78f, 0.12f}, {0.22f, 0.45f, 1.0f}, {0.95f, 0.30f, 0.85f}};
    switch (colors) {
        case sim::GarlandColors::Warm:
            return {1.0f, 0.76f, 0.42f};
        case sim::GarlandColors::Blue:
            return {0.42f, 0.70f, 1.0f};
        default:
            return kMulti[index % 5];
    }
}

// 0..1: how lit a bulb is at time t.
float BulbBrightness(sim::GarlandBlink blink, std::uint64_t windowId, std::uint32_t index, double t) {
    constexpr float kTau = 6.2831853f;
    switch (blink) {
        case sim::GarlandBlink::Steady:
            return 1.0f;
        case sim::GarlandBlink::Twinkle: {
            // Every bulb breathes at its own pace.
            const std::uint64_t key = windowId * 0x100000001B3ull ^ index;
            const float freq = 0.25f + 0.55f * Hash01(key);
            const float phase = kTau * Hash01(key ^ 0xABCDEFull);
            const float w = static_cast<float>(std::fmod(t * freq, 1.0)) * kTau + phase;
            const float v = 0.5f + 0.5f * std::sin(w);
            return 0.12f + 0.88f * v * std::sqrt(v);
        }
        case sim::GarlandBlink::Chase: {
            // Running lights: a wave travels along the window from left to right.
            const float pos = static_cast<float>(index) / 5.0f - static_cast<float>(std::fmod(t * 1.1, 1000.0));
            const float v = 0.5f + 0.5f * std::cos(kTau * pos);
            return 0.12f + 0.88f * v * v * v;
        }
    }
    return 1.0f;
}

void BuildGarlands(const sim::World& world, std::vector<Shape>& out) {
    const sim::GarlandConfig cfg = world.garlandConfig();
    if (!cfg.enabled()) return;
    constexpr Rgb kWire{0.06f, 0.15f, 0.08f};
    constexpr Rgb kSocket{0.10f, 0.22f, 0.12f};
    constexpr int kWireSegments = 8;
    const double t = world.time();
    std::vector<Shape> glows;
    for (const sim::Festoon& f : world.festoons()) {
        const float s = f.scale;
        for (int k = 0; k < kWireSegments; ++k) {
            const float u0 = static_cast<float>(k) / kWireSegments, u1 = static_cast<float>(k + 1) / kWireSegments;
            Capsule(out, f.at(u0), f.at(u1), 0.75f * s, kWire, 0.15f, 1.0f);
        }
        for (int k = 0; k < f.bulbs; ++k) {
            const std::uint32_t index = f.firstBulb + static_cast<std::uint32_t>(k);
            const sim::Vec2 p = f.at((static_cast<float>(k) + 0.5f) / static_cast<float>(f.bulbs));
            // Bulbs hang a little crooked and swing a bit further than the wire.
            const float tilt = (Hash01(f.windowId ^ (index * 7919ull)) - 0.5f) * 0.5f + f.swing * 1.15f;
            const sim::Vec2 d{std::sin(tilt), std::cos(tilt)};
            const float angle = std::atan2(d.y, d.x);
            const Rgb c = BulbColor(cfg.colors, index);
            const float br = BulbBrightness(cfg.blink, f.windowId, index, t);
            const sim::Vec2 socket = p + d * (2.2f * s);
            const sim::Vec2 bulb = p + d * (8.0f * s);
            out.push_back(mk(socket.x, socket.y, 2.2f * s, 1.7f * s, angle, kSocket.r, kSocket.g, kSocket.b, 1.0f,
                             0.3f, kCapsule, 0.0f));
            const float lit = 0.3f + 0.75f * br;
            out.push_back(mk(bulb.x, bulb.y, 4.6f * s, 2.8f * s, angle, c.r * lit, c.g * lit, c.b * lit, 1.0f,
                             0.25f + 0.45f * br, kCapsule, 0.0f));
            if (br > 0.05f) {
                const float r = (9.0f + 6.0f * br) * s;
                glows.push_back(mk(bulb.x, bulb.y, r, r, 0.0f, c.r, c.g, c.b, 0.6f * br, 0.0f, kGlow, 0.0f));
            }
        }
    }
    // Halos last, so a bulb also lights its neighbours' glass and the snow.
    out.insert(out.end(), glows.begin(), glows.end());
}

}  // namespace

std::vector<Shape> Build(const sim::World& world) {
    std::vector<Shape> out;
    BuildGarlands(world, out);
    // Snowman decorations pop in when the third ball settles: bucket hat,
    // carrot nose, button eyes, coal smile, buttons and twig arms.
    for (const sim::Snowman& m : world.snowmen()) {
        const sim::Snowball& head = world.snowballs()[m.head];
        const sim::Snowball& mid = world.snowballs()[m.middle];
        const float age = static_cast<float>(world.time() - head.decoratedAt);
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

    // The wiper: arm, blade holder, rubber blade and the pivot cap. While it
    // moves, the outer half of the blade flexes back against the motion and
    // fading copies trail behind it (motion blur), like a real wiper's smear.
    const sim::Wiper& wp = world.wiper();
    if (wp.enabled()) {
        const float sc = wp.scale();
        const sim::Vec2 pv = wp.pivot();
        const float L = wp.length();
        const float w = wp.angularVelocity();
        const float lag = std::clamp(-w * 0.011f, -0.07f, 0.07f);
        auto capsule = [&](sim::Vec2 a, sim::Vec2 b, float halfWidth, float r, float g, float bl, float shine,
                           float alpha) {
            const sim::Vec2 d = b - a;
            const float half = sim::length(d) * 0.5f + halfWidth;
            out.push_back(mk((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, half, halfWidth, std::atan2(d.y, d.x), r, g,
                             bl, alpha, shine, 0.0f, 0.0f));
        };
        auto drawWiper = [&](float angle, float bend, float alpha) {
            const sim::Vec2 d0 = sim::Wiper::direction(angle);
            const sim::Vec2 d1 = sim::Wiper::direction(angle + bend);
            const sim::Vec2 p1 = pv + d0 * (L * sim::Wiper::kBladeStart);  // blade start
            const sim::Vec2 pm = pv + d0 * (L * 0.62f);                      // flex point
            const sim::Vec2 p2 = pm + d1 * (L * 0.38f);                      // tip
            // Holder sits slightly offset from the arm, like on a car.
            const sim::Vec2 n0 = sim::Vec2{d0.y, -d0.x} * (3.0f * sc);
            const sim::Vec2 n1 = sim::Vec2{d1.y, -d1.x} * (3.0f * sc);
            capsule(p1 + n0, pm + n0, 2.0f * sc, 0.16f, 0.17f, 0.19f, 0.35f, alpha);
            capsule(pm + n1, p2 + n1, 2.0f * sc, 0.16f, 0.17f, 0.19f, 0.35f, alpha);
            capsule(p1, pm, wp.bladeHalfWidth(), 0.05f, 0.05f, 0.06f, 0.18f, alpha);
            capsule(pm, p2, wp.bladeHalfWidth(), 0.05f, 0.05f, 0.06f, 0.18f, alpha);
            capsule(pv, pm, 2.6f * sc, 0.13f, 0.14f, 0.16f, 0.45f, alpha);
            capsule(pm, p2, 2.6f * sc, 0.13f, 0.14f, 0.16f, 0.45f, alpha);
        };
        // Trail: copies at earlier angles over the last ~35 ms, fading out.
        constexpr int kTrail = 7;
        constexpr float kTrailTime = 0.035f;
        if (std::abs(w) > 0.4f) {
            for (int k = kTrail; k >= 1; --k) {
                const float t = static_cast<float>(k) / static_cast<float>(kTrail);
                const float alpha = 0.26f * (1.0f - t) + 0.04f;
                drawWiper(wp.angle() - w * kTrailTime * t, lag * (1.0f - 0.5f * t), alpha);
            }
        }
        drawWiper(wp.angle(), lag, 1.0f);
        const float pr = wp.pivotRadius();
        out.push_back(mk(pv.x, pv.y, pr * 0.75f, pr * 0.75f, 0.0f, 0.2f, 0.21f, 0.24f, 1.0f, 0.5f, 0.0f, 0.0f));
        out.push_back(mk(pv.x, pv.y, pr * 0.32f, pr * 0.32f, 0.0f, 0.08f, 0.08f, 0.09f, 1.0f, 0.2f, 0.0f, 0.0f));
    }
    return out;
}

}  // namespace shapes
