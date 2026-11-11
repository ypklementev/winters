// Snow-bearing surfaces: top edges of windows/taskbars and screen bottoms,
// plus z-order visibility and a spatial index for fast "what is under x".
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "sim/heightmap.h"
#include "sim/types.h"

namespace sim {

// A top-level window as reported by the platform layer.
struct WindowInfo {
    std::uint64_t id = 0;
    RectF rect;            // physical px, overlay space
    bool taskbar = false;  // Shell_TrayWnd / Shell_SecondaryTrayWnd
};

struct Span {
    float left = 0.0f;
    float right = 0.0f;
};

// Removes [l, r) from a sorted list of disjoint spans.
std::vector<Span> subtractSpan(const std::vector<Span>& spans, float l, float r);

// Parts of the horizontal edge y = edgeY, x in [left, right) where snow can lie:
//  - the row just above the edge must be on some monitor, at least
//    `minHeadroom` px below that monitor's top (maximized windows don't count);
//  - not covered by any occluder whose rect spans the edge
//    (occluder.top <= edgeY <= occluder.bottom).
std::vector<Span> visibleEdgeSpans(float left, float right, float edgeY,
                                   const std::vector<RectF>& occluders,
                                   const std::vector<Monitor>& monitors, float minHeadroom);

inline constexpr float kNoSurface = std::numeric_limits<float>::infinity();

struct Surface {
    std::uint64_t id = 0;
    RectF rect;  // owner window rect; edge is rect.top
    bool ground = false;
    bool taskbar = false;
    float scale = 1.0f;
    Heightmap pile;
    float spillCarry = 0.0f;    // spilled area not yet turned into particles
    float erosionCarry = 0.0f;  // wind-blown area not yet turned into particles
    double lastSeen = 0.0;     // world time of the last setWindows() that saw it
    bool settled = true;        // no relaxation needed until something changes

    float edgeY() const { return rect.top; }
    // Y of the snow top at screen x, or kNoSurface if nothing can lie there.
    float lineAt(float x) const {
        const int c = pile.cellAt(x - rect.left);
        if (c < 0 || !pile.visible(static_cast<std::size_t>(c))) return kNoSurface;
        return rect.top - pile.height(static_cast<std::size_t>(c));
    }
    float cellCenterX(std::size_t i) const {
        return rect.left + (static_cast<float>(i) + 0.5f) * pile.cellWidth();
    }
};

// Buckets surfaces by x so particles only test the few that are below them.
class SurfaceIndex {
public:
    void build(const std::vector<Surface>& surfaces, const RectF& bounds);
    const std::vector<std::uint32_t>& at(float x) const {
        if (buckets_.empty()) return empty_;
        const float f = (x - origin_) * kInvBucket;
        if (f < 0.0f || f >= static_cast<float>(buckets_.size())) return empty_;
        return buckets_[static_cast<std::size_t>(f)];
    }
    // Calls fn(surfaceIndex) once for every surface overlapping [x0, x1].
    template <class Fn>
    void forRange(float x0, float x1, Fn&& fn) const {
        if (buckets_.empty()) return;
        const int b0 = bucketOf(x0), b1 = bucketOf(x1);
        ++stamp_;
        if (seen_.size() < surfaceCount_) seen_.resize(surfaceCount_, 0);
        for (int b = b0; b <= b1; ++b) {
            for (std::uint32_t s : buckets_[static_cast<std::size_t>(b)]) {
                if (seen_[s] == stamp_) continue;
                seen_[s] = stamp_;
                fn(s);
            }
        }
    }

private:
    int bucketOf(float x) const;

    static constexpr float kBucketWidth = 64.0f;
    static constexpr float kInvBucket = 1.0f / kBucketWidth;
    float origin_ = 0.0f;
    std::vector<std::vector<std::uint32_t>> buckets_;
    std::vector<std::uint32_t> empty_;
    std::size_t surfaceCount_ = 0;
    mutable std::vector<std::uint32_t> seen_;
    mutable std::uint32_t stamp_ = 0;
};

}  // namespace sim
