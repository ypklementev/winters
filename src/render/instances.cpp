#include "render/instances.h"

#include <algorithm>

#include "render/shapes.h"
#include "sim/world.h"

namespace render {

void FrameInstances::build(const sim::World& world) {
    const sim::FlakePool& f = world.flakes();
    flakes.resize(f.size());
    for (size_t i = 0; i < f.size(); ++i) {
        float r = f.radius[i];
        float alpha = f.alpha[i];
        // Sub-pixel quads may not cover any pixel centre: draw at 1 px and fade
        // by the area ratio instead, which keeps the perceived brightness.
        if (r < 1.0f) {
            alpha *= r * r;
            r = 1.0f;
        }
        // Minified sprites lose coverage in the mips; boost small ones a bit.
        const float gain = std::clamp(2.0f - 0.25f * r, 1.0f, 1.8f);
        flakes[i] = {f.x[i], f.y[i], r, alpha, f.angle[i], f.squash(i), static_cast<float>(f.sprite[i]), gain};
    }

    balls.clear();
    for (const sim::Snowball& b : world.snowballs()) balls.push_back({b.pos.x, b.pos.y, b.radius, b.angle});
    // Lower balls first, so a snowman's upper balls overlap the ones they sit in.
    std::sort(balls.begin(), balls.end(), [](const BallInstance& a, const BallInstance& b) { return a.y > b.y; });

    // Smooth outline: corner heights are averages of neighbouring cells;
    // ends next to a hidden/absent cell are rounded down.
    piles.clear();
    constexpr float kMin = 0.05f;
    for (const sim::Surface& s : world.surfaces()) {
        const sim::Heightmap& hm = s.pile;
        const size_t n = hm.size();
        const float cw = hm.cellWidth();
        const auto& h = hm.heights();
        for (size_t i = 0; i < n; ++i) {
            if (!hm.visible(i)) continue;
            const bool leftOpen = i == 0 || !hm.visible(i - 1);
            const bool rightOpen = i + 1 >= n || !hm.visible(i + 1);
            const float hl = leftOpen ? h[i] * 0.5f : (h[i - 1] + h[i]) * 0.5f;
            const float hr = rightOpen ? h[i] * 0.5f : (h[i] + h[i + 1]) * 0.5f;
            if (h[i] < kMin && hl < kMin && hr < kMin) continue;
            const float x0 = s.rect.left + static_cast<float>(i) * cw;
            piles.push_back({x0, x0 + cw, s.edgeY(), 0.0f, hl, hr});
        }
    }

    // Wiper and snowman decorations (built in pure C++, see render/shapes.cpp).
    shapes.clear();
    for (const shapes::Shape& sh : shapes::Build(world)) {
        shapes.push_back({sh.cx, sh.cy, sh.halfLength, sh.halfWidth, sh.angle, sh.r, sh.g, sh.b, sh.alpha, sh.shine,
                          static_cast<float>(sh.type), 0.0f});
    }
}

}  // namespace render
