// Basic math/geometry types for the snow simulation.
// Pure C++: no platform headers allowed in sim/.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sim {

// Simulation runs at a fixed 60 Hz step.
inline constexpr float kFixedDt = 1.0f / 60.0f;
inline constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
constexpr Vec2 operator*(float s, Vec2 a) { return {a.x * s, a.y * s}; }
constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }

// Axis-aligned rectangle, half-open: [left, right) x [top, bottom).
// Y grows downwards (screen convention).
struct RectF {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    constexpr float width() const { return right - left; }
    constexpr float height() const { return bottom - top; }
    constexpr bool empty() const { return right <= left || bottom <= top; }
    constexpr bool contains(Vec2 p) const {
        return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
    }
    constexpr bool containsX(float x) const { return x >= left && x < right; }
};

inline RectF unite(const RectF& a, const RectF& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return {std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right),
            std::max(a.bottom, b.bottom)};
}

// A physical display, in simulation coordinates (physical pixels).
struct Monitor {
    RectF bounds;
    RectF work;          // bounds minus taskbar/appbars
    float scale = 1.0f;  // DPI / 96
};

}  // namespace sim
