#include "sim/garlands.h"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

constexpr float kGravity = 1800.0f;    // px/s^2 per unit of scale, as for snowballs
constexpr float kPendulum = 22.0f;     // effective pendulum length, logical px
constexpr float kDamping = 0.12f;      // damping ratio of the swing
constexpr float kVelSmoothing = 0.05f; // s, smoothing of the window velocity
constexpr float kWindTilt = 0.003f;    // rad per logical px/s of wind
constexpr float kMaxAngle = 1.1f;
constexpr float kMaxOmega = 16.0f;

bool insideAny(Vec2 p, const std::vector<Surface>& surfaces, std::size_t above) {
    for (std::size_t j = 0; j < above; ++j) {
        if (!surfaces[j].ground && surfaces[j].rect.contains(p)) return true;
    }
    return false;
}

}  // namespace

void Garlands::clear() {
    swing_.clear();
    festoons_.clear();
}

void Garlands::update(float dt, const std::vector<Surface>& surfaces, float wind) {
    festoons_.clear();
    for (auto& [id, sw] : swing_) sw.seen = false;

    for (std::size_t k = 0; k < surfaces.size(); ++k) {
        const Surface& s = surfaces[k];
        if (s.ground || s.taskbar) continue;
        const float sc = s.scale;

        // Swing: a damped pendulum, pushed back when the window accelerates
        // and leaning with the wind.
        auto [it, fresh] = swing_.try_emplace(s.id);
        Swing& sw = it->second;
        if (fresh) {
            sw.left = s.rect.left;
            sw.top = s.rect.top;
        }
        sw.seen = true;
        if (dt > 0.0f) {
            const float k1 = 1.0f - std::exp(-dt / kVelSmoothing);
            const float vx = sw.vx + ((s.rect.left - sw.left) / dt - sw.vx) * k1;
            const float vy = sw.vy + ((s.rect.top - sw.top) / dt - sw.vy) * k1;
            const float ax = (vx - sw.vx) / dt;
            const float ay = (vy - sw.vy) / dt;
            sw.vx = vx;
            sw.vy = vy;
            sw.left = s.rect.left;
            sw.top = s.rect.top;

            const float L = kPendulum * sc;
            // Lifting the window yanks the lights down harder (and vice versa).
            const float g = std::max(kGravity * sc * 0.2f, kGravity * sc - ay);
            const float w0 = std::sqrt(g / L);
            const float tilt = std::clamp(wind * kWindTilt, -0.45f, 0.45f);
            const float accel = -w0 * w0 * (std::sin(sw.angle) - tilt) - 2.0f * kDamping * w0 * sw.omega -
                                ax * std::cos(sw.angle) / L;
            sw.omega = std::clamp(sw.omega + accel * dt, -kMaxOmega, kMaxOmega);
            sw.angle = std::clamp(sw.angle + sw.omega * dt, -kMaxAngle, kMaxAngle);
        }

        // Festoons over every visible stretch of the top edge.
        const Heightmap& hm = s.pile;
        const float cw = hm.cellWidth();
        const Vec2 sagDir{std::sin(sw.angle), std::cos(sw.angle)};
        std::uint32_t bulbIndex = 0;
        std::size_t i = 0;
        while (i < hm.size()) {
            if (!hm.visible(i)) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < hm.size() && hm.visible(j)) ++j;
            const float x0 = s.rect.left + static_cast<float>(i) * cw;
            const float x1 = std::min(s.rect.left + static_cast<float>(j) * cw, s.rect.right);
            i = j;
            const float width = x1 - x0;
            if (width < kMinRun * sc) continue;
            const int n = std::max(1, static_cast<int>(std::lround(width / (kSpan * sc))));
            const float span = width / static_cast<float>(n);
            for (int f = 0; f < n; ++f) {
                Festoon fs;
                fs.a = {x0 + span * static_cast<float>(f), s.edgeY()};
                fs.b = {fs.a.x + span, s.edgeY()};
                fs.sag = sagDir * (kSag * sc);
                fs.scale = sc;
                fs.swing = sw.angle;
                fs.windowId = s.id;
                fs.bulbs = std::max(2, static_cast<int>(std::lround(span / (kBulbSpacing * sc))));
                // Covered by a window in front (below the edge, where the
                // snow-line visibility doesn't look): leave this span out.
                bool hidden = false;
                for (float u : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
                    const Vec2 p = fs.at(u);
                    if (insideAny(p, surfaces, k) || insideAny(p + Vec2{0.0f, kHang * sc}, surfaces, k)) {
                        hidden = true;
                        break;
                    }
                }
                if (hidden) continue;
                fs.firstBulb = bulbIndex;
                bulbIndex += static_cast<std::uint32_t>(fs.bulbs);
                festoons_.push_back(fs);
            }
        }
    }

    // Forget windows that are gone.
    for (auto it = swing_.begin(); it != swing_.end();) {
        it = it->second.seen ? std::next(it) : swing_.erase(it);
    }
}

}  // namespace sim
