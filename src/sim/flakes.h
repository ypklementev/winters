// Snowflake particle pool (structure of arrays) and its integrator.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sim/types.h"

namespace sim {

class WindField;

struct FlakeSpawn {
    Vec2 pos;
    Vec2 vel;
    float radius = 1.0f;      // visual radius, px
    float fallSpeed = 50.0f;  // terminal fall speed in still air, px/s
    float response = 3.0f;    // how fast velocity follows the air, 1/s
    float flutterAmp = 8.0f;  // side-to-side flutter speed, px/s
    float flutterFreq = 1.5f; // rad/s
    float phase = 0.0f;       // flutter phase, rad
    float angle = 0.0f;       // sprite rotation, rad
    float spin = 0.0f;        // rad/s
    float alpha = 1.0f;
    float scale = 1.0f;       // DPI scale of the monitor it was born on
    float mass = 0.5f;        // snow it deposits on landing, px^2
    std::uint16_t sprite = 0;
    bool tumbles = false;     // flat crystal that flips while falling
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

    bool add(const FlakeSpawn& s);  // false if full
    void remove(std::size_t i);
    void clear();

    // Visual squash factor (1 = face-on, ~0.3 = edge-on) of a tumbling flake.
    float squash(std::size_t i) const;

    std::vector<float> x, y, py;  // position; y before the last step
    std::vector<float> vx, vy;    // velocity without flutter, px/s
    std::vector<float> fallSpeed;
    std::vector<float> drag;      // per-step relaxation factor (for kFixedDt)
    std::vector<float> flutter;   // flutter amplitude, px/s
    std::vector<float> oscX, oscY;  // unit phasor of the flutter/tumble phase
    std::vector<float> oscC, oscS;  // per-step phasor rotation (cos, sin)
    std::vector<float> angle, spin;
    std::vector<float> radius, alpha, scale, mass;
    std::vector<std::uint16_t> sprite;
    std::vector<std::uint8_t> tumbles;

private:
    template <class Fn>
    void forEachArray(Fn&& fn);

    std::size_t capacity_ = 0;
};

// Advances all flakes by dt through the air field (nullptr = still air).
void integrateFlakes(FlakePool& pool, float dt, const WindField* air);

}  // namespace sim
