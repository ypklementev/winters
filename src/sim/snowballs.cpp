#include "sim/snowballs.h"

#include <cmath>

namespace sim {

namespace {
constexpr float kRestitution = 0.05f;
constexpr float kSteepContact = 0.6f;  // |normal.y| above this => sticky stacking contact
constexpr float kSlideFriction = 0.3f;
}  // namespace

void resolveBallContacts(std::vector<Snowball>& balls, int iterations) {
    const std::size_t n = balls.size();
    for (int it = 0; it < iterations; ++it) {
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                Snowball& a = balls[i];
                Snowball& b = balls[j];
                const Vec2 d = b.pos - a.pos;
                const float minD = a.radius + b.radius;
                const float dist2 = dot(d, d);
                if (dist2 >= minD * minD || dist2 < 1e-8f) continue;
                const float dist = std::sqrt(dist2);
                const Vec2 nrm = d * (1.0f / dist);

                const float wa = a.held ? 0.0f : 1.0f / (a.radius * a.radius);
                const float wb = b.held ? 0.0f : 1.0f / (b.radius * b.radius);
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

}  // namespace sim
