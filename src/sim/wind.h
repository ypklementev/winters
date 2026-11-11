// Air motion: gusty base wind plus divergence-free turbulence (eddies that
// drift with the wind). Evaluated on a coarse grid once per step and sampled
// bilinearly per particle, so thousands of flakes cost almost nothing.
#pragma once

#include <vector>

#include "sim/types.h"

namespace sim {

class WindField {
public:
    // Grid covering `bounds` (plus a margin), node spacing `cell` px.
    void configure(const RectF& bounds, float cell = 64.0f);

    // baseWind: horizontal wind, px/s. turbulence: eddy speed scale, px/s.
    void update(double time, float baseWind, float turbulence);

    // Air velocity at (x, y), px/s (at DPI scale 1). Zero if not configured.
    Vec2 sample(float x, float y) const;

    float baseWind() const { return base_; }

private:
    float originX_ = 0.0f, originY_ = 0.0f;
    float invCell_ = 1.0f / 64.0f;
    float cell_ = 64.0f;
    int nx_ = 0, ny_ = 0;
    std::vector<Vec2> nodes_;
    std::vector<float> colSin_, colCos_, rowSin_, rowCos_;
    float base_ = 0.0f;
};

}  // namespace sim
