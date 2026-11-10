#include "sim/snowballs.h"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {
constexpr float kRestitution = 0.05f;
constexpr float kSteepContact = 0.6f;  // |normal.y| above this => sticky stacking contact
constexpr float kSlideFriction = 0.3f;
constexpr float kSmallerBy = 0.92f;    // each snowman ball must be < 92% of the one below
}  // namespace

int findBall(const std::vector<Snowball>& balls, std::uint32_t id) {
    if (id == 0) return -1;
    for (std::size_t i = 0; i < balls.size(); ++i) {
        if (balls[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

void resolveBallContacts(std::vector<Snowball>& balls, int iterations) {
    const std::size_t n = balls.size();
    for (int it = 0; it < iterations; ++it) {
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                Snowball& a = balls[i];
                Snowball& b = balls[j];
                // Glued together (ids start at 1; 0 means "none").
                if ((a.parent != 0 && a.parent == b.id) || (b.parent != 0 && b.parent == a.id)) continue;
                const Vec2 d = b.pos - a.pos;
                const float minD = a.radius + b.radius;
                const float dist2 = dot(d, d);
                if (dist2 >= minD * minD || dist2 < 1e-8f) continue;
                const float dist = std::sqrt(dist2);
                const Vec2 nrm = d * (1.0f / dist);

                const float wa = a.kinematic() ? 0.0f : 1.0f / (a.radius * a.radius);
                const float wb = b.kinematic() ? 0.0f : 1.0f / (b.radius * b.radius);
                const float wsum = wa + wb;
                if (wsum <= 0.0f) continue;
                a.contact = b.contact = true;

                const bool steep = std::abs(nrm.y) > kSteepContact;
                Vec2 corr;
                if (steep) {
                    // Separate vertically only: keeps a ball balanced on top of another.
                    const float need = std::sqrt(std::max(minD * minD - d.x * d.x, 0.0f));
                    corr = {0.0f, (need - std::abs(d.y)) * (d.y >= 0.0f ? 1.0f : -1.0f)};
                } else {
                    corr = nrm * (minD - dist);
                }
                a.pos = a.pos - corr * (wa / wsum);
                b.pos = b.pos + corr * (wb / wsum);

                const Vec2 rv = b.vel - a.vel;
                const float vn = dot(rv, nrm);
                if (vn < 0.0f) {
                    // A ball carried in the hand bumps others but never breaks them.
                    if (!a.held && !b.held) {
                        a.impactSpeed = std::max(a.impactSpeed, -vn);
                        b.impactSpeed = std::max(b.impactSpeed, -vn);
                    }
                    const float jn = -(1.0f + kRestitution) * vn / wsum;
                    a.vel = a.vel - nrm * (jn * wa);
                    b.vel = b.vel + nrm * (jn * wb);
                }
                const Vec2 tng{-nrm.y, nrm.x};
                const float vt = dot(b.vel - a.vel, tng);
                const float jt = -vt / wsum * (steep ? 1.0f : kSlideFriction);
                a.vel = a.vel - tng * (jt * wa);
                b.vel = b.vel + tng * (jt * wb);
            }
        }
    }
}

std::vector<Snowman> findSnowmen(const std::vector<Snowball>& balls) {
    std::vector<Snowman> out;
    auto childOf = [&](std::size_t p) {
        int found = -1;
        int count = 0;
        for (std::size_t i = 0; i < balls.size(); ++i) {
            if (balls[i].parent == balls[p].id) {
                found = static_cast<int>(i);
                ++count;
            }
        }
        return count == 1 ? found : -1;  // a single neat stack only
    };
    for (std::size_t base = 0; base < balls.size(); ++base) {
        if (balls[base].parent != 0) continue;
        const int mid = childOf(base);
        if (mid < 0) continue;
        const int head = childOf(static_cast<std::size_t>(mid));
        if (head < 0) continue;
        if (childOf(static_cast<std::size_t>(head)) >= 0) continue;  // a fourth ball on top
        const Snowball& b = balls[base];
        const Snowball& m = balls[static_cast<std::size_t>(mid)];
        const Snowball& h = balls[static_cast<std::size_t>(head)];
        if (m.radius > b.radius * kSmallerBy || h.radius > m.radius * kSmallerBy) continue;
        // Upright: each ball sits roughly above the one below.
        if (m.pos.y >= b.pos.y || h.pos.y >= m.pos.y) continue;
        if (std::abs(m.pos.x - b.pos.x) > b.radius * 0.6f || std::abs(h.pos.x - m.pos.x) > m.radius * 0.6f) continue;
        out.push_back({base, static_cast<std::size_t>(mid), static_cast<std::size_t>(head)});
    }
    return out;
}

}  // namespace sim
