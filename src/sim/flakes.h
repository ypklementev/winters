// Snowflake particle pool (structure of arrays) and its integrator.
#pragma once

#include <cstddef>
#include <vector>

#include "sim/types.h"

namespace sim {

struct FlakeSpawn {
    Vec2 pos;
    Vec2 vel;
    float radius = 2.0f;     // px
    float fallSpeed = 60.f;  // terminal fall speed, px/s
    float swayAmp = 10.f;    // horizontal sway velocity amplitude, px/s
    float swayFreq = 1.f;    // rad/s
    float phase = 0.f;       // rad
    float alpha = 1.f;
    float scale = 1.f;       // DPI scale of the monitor it was born on
};

// Fixed-capacity pool. Removal swaps with the last element, so indices are
// not stable across remove().
class FlakePool {
public:
    explicit FlakePool(std::size_t capacity = 0) { setCapacity(capacity); }

    void setCapacity(std::size_t capacity);
    std::size_t capacity() const { return capacity_; }
    std::size_t size() const { return x.size(); }
    bool full() const { return size() >= capacity_; }

    // Returns false if the pool is full.
    bool add(const FlakeSpawn& s);
    void remove(std::size_t i);
    void clear();

    std::vector<float> x, y;          // centre position, px
    std::vector<float> py;            // y before the last integration step
    std::vector<float> vx, vy;        // base velocity (without sway), px/s
    std::vector<float> radius;
    std::vector<float> fallSpeed;
    std::vector<float> swayAmp, swayFreq, phase;
    std::vector<float> alpha;
    std::vector<float> scale;

private:
    std::size_t capacity_ = 0;
};

// How quickly a flake's velocity relaxes to its target (wind + fall), 1/s.
inline constexpr float kFlakeDrag = 2.5f;

// Advance all flakes by dt. `wind` is horizontal air speed in px/s at scale 1.
// `time` is the simulation clock used for the sway phase.
void integrateFlakes(FlakePool& pool, float dt, float wind, float time);

}  // namespace sim
