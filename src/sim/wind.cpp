#include "sim/wind.h"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

// Stream function psi = A sin(kx*x + a(t)) sin(ky*y + b(t)) per mode;
// velocity u = dpsi/dy, v = -dpsi/dx is divergence-free (real swirls).
struct Mode {
    float kx, ky;      // rad/px
    float speed;       // relative velocity amplitude
    float wx, wy;      // phase drift, rad/s
    float px, py;      // phase offsets
};

constexpr Mode kModes[] = {
    {2.0f * kPi / 900.0f, 2.0f * kPi / 700.0f, 1.00f, 0.11f, 0.07f, 0.3f, 1.7f},
    {2.0f * kPi / 380.0f, 2.0f * kPi / 330.0f, 0.55f, -0.23f, 0.19f, 2.1f, 0.4f},
    {2.0f * kPi / 170.0f, 2.0f * kPi / 210.0f, 0.30f, 0.41f, -0.33f, 4.2f, 2.9f},
};

// Updrafts are weaker than sideways swirls, so snow keeps falling on average.
constexpr float kVerticalScale = 0.45f;

}  // namespace

void WindField::configure(const RectF& bounds, float cell) {
    cell_ = cell;
    invCell_ = 1.0f / cell;
    const float margin = cell * 4.0f;
    originX_ = bounds.left - margin;
    originY_ = bounds.top - margin;
    nx_ = static_cast<int>(std::ceil((bounds.width() + 2.0f * margin) * invCell_)) + 2;
    ny_ = static_cast<int>(std::ceil((bounds.height() + 2.0f * margin) * invCell_)) + 2;
    nodes_.assign(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_), Vec2{});
}

void WindField::update(double time, float baseWind, float turbulence) {
    base_ = baseWind;
    if (nodes_.empty()) return;
    // Each mode is separable: sin/cos of the x-phase depend only on the column
    // and of the y-phase only on the row, so trig is O(nx + ny), not O(nx * ny).
    constexpr int kM = static_cast<int>(sizeof(kModes) / sizeof(kModes[0]));
    colSin_.resize(static_cast<std::size_t>(nx_) * kM);
    colCos_.resize(colSin_.size());
    rowSin_.resize(static_cast<std::size_t>(ny_) * kM);
    rowCos_.resize(rowSin_.size());
    // Phases in double: `time` keeps growing for as long as the app runs.
    const double drift = static_cast<double>(baseWind) * time * 0.8;  // eddies are carried by the wind
    for (int i = 0; i < nx_; ++i) {
        const double x = static_cast<double>(originX_ + static_cast<float>(i) * cell_) - drift;
        for (int m = 0; m < kM; ++m) {
            const float a = static_cast<float>(std::fmod(kModes[m].kx * x + kModes[m].wx * time + kModes[m].px, 2.0 * kPi));
            colSin_[static_cast<std::size_t>(i * kM + m)] = std::sin(a);
            colCos_[static_cast<std::size_t>(i * kM + m)] = std::cos(a);
        }
    }
    for (int j = 0; j < ny_; ++j) {
        const float y = originY_ + static_cast<float>(j) * cell_;
        for (int m = 0; m < kM; ++m) {
            const float a = static_cast<float>(std::fmod(kModes[m].ky * y + kModes[m].wy * time + kModes[m].py, 2.0 * kPi));
            rowSin_[static_cast<std::size_t>(j * kM + m)] = std::sin(a);
            rowCos_[static_cast<std::size_t>(j * kM + m)] = std::cos(a);
        }
    }
    for (int j = 0; j < ny_; ++j) {
        for (int i = 0; i < nx_; ++i) {
            float u = 0.0f, v = 0.0f;
            for (int m = 0; m < kM; ++m) {
                const float sx = colSin_[static_cast<std::size_t>(i * kM + m)];
                const float cx = colCos_[static_cast<std::size_t>(i * kM + m)];
                const float sy = rowSin_[static_cast<std::size_t>(j * kM + m)];
                const float cy = rowCos_[static_cast<std::size_t>(j * kM + m)];
                // u = dpsi/dy, v = -dpsi/dx, normalised so `speed` is the peak.
                u += kModes[m].speed * sx * cy;
                v -= kModes[m].speed * cx * sy * (kModes[m].kx / kModes[m].ky);
            }
            nodes_[static_cast<std::size_t>(j) * static_cast<std::size_t>(nx_) + static_cast<std::size_t>(i)] = {
                baseWind + u * turbulence, v * turbulence * kVerticalScale};
        }
    }
}

Vec2 WindField::sample(float x, float y) const {
    if (nodes_.empty()) return {};
    float fx = (x - originX_) * invCell_;
    float fy = (y - originY_) * invCell_;
    fx = std::clamp(fx, 0.0f, static_cast<float>(nx_ - 2));
    fy = std::clamp(fy, 0.0f, static_cast<float>(ny_ - 2));
    const int i = static_cast<int>(fx);
    const int j = static_cast<int>(fy);
    const float tx = fx - static_cast<float>(i);
    const float ty = fy - static_cast<float>(j);
    const std::size_t row = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx_);
    const Vec2 a = nodes_[row + static_cast<std::size_t>(i)];
    const Vec2 b = nodes_[row + static_cast<std::size_t>(i) + 1];
    const Vec2 c = nodes_[row + static_cast<std::size_t>(nx_) + static_cast<std::size_t>(i)];
    const Vec2 d = nodes_[row + static_cast<std::size_t>(nx_) + static_cast<std::size_t>(i) + 1];
    const Vec2 top = a + (b - a) * tx;
    const Vec2 bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

}  // namespace sim
