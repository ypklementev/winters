#include "sim/flakes.h"

#include <cmath>

namespace sim {

void FlakePool::setCapacity(std::size_t capacity) {
    capacity_ = capacity;
    for (auto* v : {&x, &y, &py, &vx, &vy, &radius, &fallSpeed, &swayAmp, &swayFreq, &phase, &alpha,
                    &scale}) {
        if (v->size() > capacity) v->resize(capacity);
        v->reserve(capacity);
    }
}

bool FlakePool::add(const FlakeSpawn& s) {
    if (full()) return false;
    x.push_back(s.pos.x);
    y.push_back(s.pos.y);
    py.push_back(s.pos.y);
    vx.push_back(s.vel.x);
    vy.push_back(s.vel.y);
    radius.push_back(s.radius);
    fallSpeed.push_back(s.fallSpeed);
    swayAmp.push_back(s.swayAmp);
    swayFreq.push_back(s.swayFreq);
    phase.push_back(s.phase);
    alpha.push_back(s.alpha);
    scale.push_back(s.scale);
    return true;
}

void FlakePool::remove(std::size_t i) {
    const std::size_t last = size() - 1;
    for (auto* v : {&x, &y, &py, &vx, &vy, &radius, &fallSpeed, &swayAmp, &swayFreq, &phase, &alpha,
                    &scale}) {
        (*v)[i] = (*v)[last];
        v->pop_back();
    }
}

void FlakePool::clear() {
    for (auto* v : {&x, &y, &py, &vx, &vy, &radius, &fallSpeed, &swayAmp, &swayFreq, &phase, &alpha,
                    &scale}) {
        v->clear();
    }
}

void integrateFlakes(FlakePool& p, float dt, float wind, float time) {
    // Exponential relaxation towards (wind, fallSpeed): stable for any dt and
    // also brings fast "ejected" particles (sweeps, avalanches) back to drifting.
    const float k = 1.0f - std::exp(-kFlakeDrag * dt);
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        const float targetVx = wind * p.scale[i];
        const float targetVy = p.fallSpeed[i];
        p.vx[i] += (targetVx - p.vx[i]) * k;
        p.vy[i] += (targetVy - p.vy[i]) * k;
        const float sway = p.swayAmp[i] * std::cos(p.phase[i] + p.swayFreq[i] * time);
        p.py[i] = p.y[i];
        p.x[i] += (p.vx[i] + sway) * dt;
        p.y[i] += p.vy[i] * dt;
    }
}

}  // namespace sim
