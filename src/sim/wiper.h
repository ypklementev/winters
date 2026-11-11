// A car windshield wiper on the screen: an arm pivoting at `pivot` that sweeps
// an arc in intermittent mode (one wipe out and back, then a pause).
// Angles are measured like on paper: 0 = pointing right, pi/2 = straight up
// (screen y grows downwards, so the direction is (cos a, -sin a)).
#pragma once

#include "sim/types.h"

namespace sim {

class Wiper {
public:
    static constexpr float kRestAngle = 0.26f;   // ~15 degrees: parked on the right
    static constexpr float kFarAngle = 2.88f;    // ~165 degrees
    static constexpr float kWipeTime = 1.3f;     // out and back, s
    static constexpr float kPauseTime = 1.1f;    // intermittent dwell, s
    static constexpr float kBladeStart = 0.3f;   // rubber covers [0.3, 1] of the arm

    void place(Vec2 pivot, float length, float scale);
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }

    // Advances the sweep. Keeps the previous angle for swept-area tests.
    void step(float dt);

    Vec2 pivot() const { return pivot_; }
    void setPivot(Vec2 p) { pivot_ = p; }
    float length() const { return length_; }
    float scale() const { return scale_; }
    float angle() const { return angle_; }
    float prevAngle() const { return prevAngle_; }
    float angularVelocity() const { return angVel_; }

    static Vec2 direction(float a) { return {std::cos(a), -std::sin(a)}; }
    Vec2 tip() const { return pivot_ + direction(angle_) * length_; }
    Vec2 bladeStart() const { return pivot_ + direction(angle_) * (length_ * kBladeStart); }
    float bladeHalfWidth() const { return 4.0f * scale_; }
    float pivotRadius() const { return 12.0f * scale_; }

    // Velocity of the arm at point p (rigid rotation about the pivot).
    Vec2 velocityAt(Vec2 p) const;

    // Was p (with extra radius `pad`) swept by the blade during the last step?
    bool swept(Vec2 p, float pad) const;

    // Angle of the sweep cycle at time t (exposed for tests).
    static float angleAt(float t);

private:
    bool enabled_ = false;
    Vec2 pivot_;
    float length_ = 300.0f;
    float scale_ = 1.0f;
    float time_ = 0.0f;
    float angle_ = kRestAngle;
    float prevAngle_ = kRestAngle;
    float angVel_ = 0.0f;
};

}  // namespace sim
