#include "sim/flakes.h"

#include <cmath>

#include "sim/wind.h"

namespace sim {

template <class Fn>
void FlakePool::forEachArray(Fn&& fn) {
    for (auto* v : {&x, &y, &py, &vx, &vy, &fallSpeed, &drag, &flutter, &oscX, &oscY, &oscC, &oscS, &angle,
                    &spin, &radius, &alpha, &scale, &mass}) {
        fn(*v);
    }
    fn(sprite);
    fn(tumbles);
}

void FlakePool::setCapacity(std::size_t capacity) {
    capacity_ = capacity;
    forEachArray([&](auto& v) {
        if (v.size() > capacity) v.resize(capacity);
        v.reserve(capacity);
    });
}

bool FlakePool::add(const FlakeSpawn& s) {
    if (full()) return false;
    x.push_back(s.pos.x);
    y.push_back(s.pos.y);
    py.push_back(s.pos.y);
    vx.push_back(s.vel.x);
    vy.push_back(s.vel.y);
    fallSpeed.push_back(s.fallSpeed);
    drag.push_back(1.0f - std::exp(-s.response * kFixedDt));
    flutter.push_back(s.flutterAmp);
    oscX.push_back(std::cos(s.phase));
    oscY.push_back(std::sin(s.phase));
    oscC.push_back(std::cos(s.flutterFreq * kFixedDt));
    oscS.push_back(std::sin(s.flutterFreq * kFixedDt));
    angle.push_back(s.angle);
    spin.push_back(s.spin);
    radius.push_back(s.radius);
    alpha.push_back(s.alpha);
    scale.push_back(s.scale);
    mass.push_back(s.mass);
    sprite.push_back(s.sprite);
    tumbles.push_back(s.tumbles ? 1 : 0);
    return true;
}

void FlakePool::remove(std::size_t i) {
    const std::size_t last = size() - 1;
    forEachArray([&](auto& v) {
        v[i] = v[last];
        v.pop_back();
    });
}

void FlakePool::clear() {
    forEachArray([](auto& v) { v.clear(); });
}

float FlakePool::squash(std::size_t i) const {
    if (!tumbles[i]) return 1.0f;
    // Edge-on when the flutter swings fastest sideways.
    return 0.3f + 0.7f * std::abs(oscY[i]);
}

void integrateFlakes(FlakePool& p, float dt, const WindField* air) {
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = air ? air->sample(p.x[i], p.y[i]) : Vec2{};
        const float s = p.scale[i];
        // Velocity relaxes towards "air + terminal fall"; light flakes have a
        // larger drag factor and follow the air more closely.
        const float k = p.drag[i];
        p.vx[i] += (a.x * s - p.vx[i]) * k;
        p.vy[i] += (a.y * s + p.fallSpeed[i] - p.vy[i]) * k;

        // Flutter phasor: rotate by a precomputed step (no trig per flake),
        // renormalise to stop drift.
        const float ox = p.oscX[i] * p.oscC[i] - p.oscY[i] * p.oscS[i];
        const float oy = p.oscX[i] * p.oscS[i] + p.oscY[i] * p.oscC[i];
        const float norm = 1.5f - 0.5f * (ox * ox + oy * oy);
        p.oscX[i] = ox * norm;
        p.oscY[i] = oy * norm;

        p.angle[i] += p.spin[i] * dt;
        p.py[i] = p.y[i];
        p.x[i] += (p.vx[i] + p.flutter[i] * p.oscX[i]) * dt;
        // Falling leaves dip a little at the ends of each swing.
        p.y[i] += (p.vy[i] + 0.25f * p.flutter[i] * p.oscY[i] * p.oscY[i]) * dt;
    }
}

}  // namespace sim
