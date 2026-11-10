#include "sim/wiper.h"

#include <algorithm>
#include <cmath>

namespace sim {

void Wiper::place(Vec2 pivot, float length, float scale) {
    pivot_ = pivot;
    length_ = length;
    scale_ = scale;
}

void Wiper::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    // Start from the parked position at the beginning of a wipe.
    time_ = 0.0f;
    angle_ = prevAngle_ = kRestAngle;
    angVel_ = 0.0f;
}

float Wiper::angleAt(float t) {
    const float cycle = kWipeTime + kPauseTime;
    const float u = std::fmod(t, cycle);
    if (u >= kWipeTime) return kRestAngle;
    // Out and back with ease-in/out: the blade slows down at both ends.
    const float s = u / kWipeTime;
    const float tri = s < 0.5f ? s * 2.0f : (1.0f - s) * 2.0f;
    const float e = tri * tri * (3.0f - 2.0f * tri);
    return kRestAngle + (kFarAngle - kRestAngle) * e;
}

void Wiper::step(float dt) {
    if (!enabled_) return;
    time_ += dt;
    prevAngle_ = angle_;
    angle_ = angleAt(time_);
    angVel_ = (angle_ - prevAngle_) / dt;
}

Vec2 Wiper::velocityAt(Vec2 p) const {
    // r = |r| (cos a, -sin a)  =>  dr/dt = w |r| (-sin a, -cos a) = w (r.y, -r.x).
    const Vec2 r = p - pivot_;
    return Vec2{r.y, -r.x} * angVel_;
}

bool Wiper::swept(Vec2 p, float pad) const {
    const Vec2 r = p - pivot_;
    const float dist2 = dot(r, r);
    const float inner = length_ * kBladeStart - pad;
    const float outer = length_ + pad;
    if (dist2 > outer * outer || (inner > 0.0f && dist2 < inner * inner)) return false;
    const float dist = std::sqrt(dist2);
    if (dist < 1e-3f) return false;
    // Angle of p in the wiper's convention, compared with the swept interval
    // widened by the blade thickness (as an angle at this radius).
    const float a = std::atan2(-r.y, r.x);
    const float margin = (bladeHalfWidth() + pad) / dist;
    const float lo = std::min(prevAngle_, angle_) - margin;
    const float hi = std::max(prevAngle_, angle_) + margin;
    return a >= lo && a <= hi;
}

}  // namespace sim
