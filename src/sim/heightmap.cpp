#include "sim/heightmap.h"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {
// The outermost cell may stand this many "slope steps" tall before snow
// starts sliding off the edge (a little overhang looks natural).
constexpr float kEdgeAllowance = 2.0f;
// Gentle area-conserving diffusion that rounds off single-flake spikes.
constexpr float kSmoothing = 0.06f;
constexpr float kSmoothEpsilon = 0.01f;  // below this the pile counts as settled
}  // namespace

Heightmap::Heightmap(std::size_t cells, float cellWidth)
    : cellW_(cellWidth), h_(cells, 0.0f), vis_(cells, 1) {}

int Heightmap::cellAt(float localX) const {
    if (localX < 0.0f) return -1;
    const auto i = static_cast<std::size_t>(localX / cellW_);
    return i < h_.size() ? static_cast<int>(i) : -1;
}

float Heightmap::heightAt(float localX) const {
    const int i = cellAt(localX);
    return (i >= 0 && vis_[i]) ? h_[i] : 0.0f;
}

float Heightmap::deposit(float localX, float spread, float area, float maxHeight) {
    if (h_.empty() || area <= 0.0f) return 0.0f;
    const float reach = spread + cellW_;
    const int first = std::max(0, static_cast<int>(std::floor((localX - reach) / cellW_)));
    const int last =
        std::min(static_cast<int>(h_.size()) - 1, static_cast<int>(std::floor((localX + reach) / cellW_)));
    if (first > last) return 0.0f;

    float weightSum = 0.0f;
    for (int i = first; i <= last; ++i) {
        if (!vis_[i]) continue;
        const float cx = (static_cast<float>(i) + 0.5f) * cellW_;
        weightSum += std::max(0.0f, 1.0f - std::abs(cx - localX) / reach);
    }
    if (weightSum <= 0.0f) return 0.0f;

    float deposited = 0.0f;
    for (int i = first; i <= last; ++i) {
        if (!vis_[i]) continue;
        const float cx = (static_cast<float>(i) + 0.5f) * cellW_;
        const float w = std::max(0.0f, 1.0f - std::abs(cx - localX) / reach) / weightSum;
        if (w <= 0.0f) continue;
        const float add = std::min(area * w / cellW_, std::max(0.0f, maxHeight - h_[i]));
        h_[i] += add;
        deposited += add * cellW_;
    }
    return deposited;
}

float Heightmap::lowerTo(std::size_t i, float newHeight) {
    newHeight = std::max(newHeight, 0.0f);
    if (h_[i] <= newHeight) return 0.0f;
    const float removed = (h_[i] - newHeight) * cellW_;
    h_[i] = newHeight;
    return removed;
}

float Heightmap::totalArea() const {
    float s = 0.0f;
    for (float v : h_) s += v;
    return s * cellW_;
}

void Heightmap::clear() { std::fill(h_.begin(), h_.end(), 0.0f); }

bool Heightmap::relax(float maxSlope, int iterations, std::vector<Spill>& spills) {
    const std::size_t n = h_.size();
    if (n == 0) return false;
    const float lim = maxSlope * cellW_;
    const float edgeLim = lim * kEdgeAllowance;
    bool moved = false;

    auto pair = [&](std::size_t a, std::size_t b) {
        float d = h_[a] - h_[b];
        const float smooth = d * kSmoothing;
        if (std::abs(smooth) > kSmoothEpsilon) {
            h_[a] -= smooth;
            h_[b] += smooth;
            d -= 2.0f * smooth;
            moved = true;
        }
        if (d > lim) {
            const float t = (d - lim) * 0.5f;
            h_[a] -= t;
            h_[b] += t;
            moved = true;
        } else if (-d > lim) {
            const float t = (-d - lim) * 0.5f;
            h_[a] += t;
            h_[b] -= t;
            moved = true;
        }
    };
    auto edge = [&](std::size_t i, float spillX) {
        if (h_[i] > edgeLim) {
            const float t = (h_[i] - edgeLim) * 0.5f;
            h_[i] -= t;
            spills.push_back({spillX, t * cellW_});
            moved = true;
        }
    };
    auto visit = [&](std::size_t i) {
        if (!vis_[i]) return;
        if (i + 1 < n && vis_[i + 1]) {
            pair(i, i + 1);
        } else {
            edge(i, (static_cast<float>(i) + 1.5f) * cellW_);
        }
        if (i == 0 || !vis_[i - 1]) edge(i, (static_cast<float>(i) - 0.5f) * cellW_);
    };

    // Alternate sweep direction to avoid a sideways bias.
    for (int it = 0; it < iterations; ++it) {
        if (it % 2 == 0) {
            for (std::size_t i = 0; i < n; ++i) visit(i);
        } else {
            for (std::size_t i = n; i-- > 0;) visit(i);
        }
    }
    return moved;
}

void Heightmap::resample(std::size_t newCells) {
    const std::size_t n = h_.size();
    if (newCells == n) return;
    const float before = totalArea();
    std::vector<float> out(newCells, 0.0f);
    if (n > 0 && newCells > 0) {
        for (std::size_t j = 0; j < newCells; ++j) {
            const float src = (static_cast<float>(j) + 0.5f) * static_cast<float>(n) /
                                  static_cast<float>(newCells) - 0.5f;
            const float s = std::clamp(src, 0.0f, static_cast<float>(n - 1));
            const auto i0 = static_cast<std::size_t>(s);
            const std::size_t i1 = std::min(i0 + 1, n - 1);
            const float t = s - static_cast<float>(i0);
            out[j] = h_[i0] * (1.0f - t) + h_[i1] * t;
        }
    }
    h_ = std::move(out);
    vis_.assign(newCells, 1);
    const float after = totalArea();
    if (after > 0.0f) {
        const float k = before / after;
        for (float& v : h_) v *= k;
    }
}

}  // namespace sim
