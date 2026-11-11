#include "sim/surfaces.h"

#include <algorithm>
#include <cmath>

namespace sim {

std::vector<Span> subtractSpan(const std::vector<Span>& spans, float l, float r) {
    std::vector<Span> out;
    out.reserve(spans.size() + 1);
    for (const Span& s : spans) {
        if (r <= s.left || l >= s.right) {
            out.push_back(s);
            continue;
        }
        if (l > s.left) out.push_back({s.left, l});
        if (r < s.right) out.push_back({r, s.right});
    }
    return out;
}

std::vector<Span> visibleEdgeSpans(float left, float right, float edgeY,
                                   const std::vector<RectF>& occluders,
                                   const std::vector<Monitor>& monitors, float minHeadroom) {
    // Start from the parts of [left, right) that sit on a monitor.
    std::vector<Span> spans;
    const float row = edgeY - 1.0f;
    for (const Monitor& m : monitors) {
        if (row < m.bounds.top || row >= m.bounds.bottom) continue;
        if (edgeY - m.bounds.top < minHeadroom) continue;
        const float l = std::max(left, m.bounds.left);
        const float r = std::min(right, m.bounds.right);
        if (l < r) spans.push_back({l, r});
    }
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.left < b.left; });
    // Merge touching spans from adjacent monitors.
    std::vector<Span> merged;
    for (const Span& s : spans) {
        if (!merged.empty() && s.left <= merged.back().right) {
            merged.back().right = std::max(merged.back().right, s.right);
        } else {
            merged.push_back(s);
        }
    }

    for (const RectF& o : occluders) {
        if (o.top <= edgeY && o.bottom >= edgeY) merged = subtractSpan(merged, o.left, o.right);
        if (merged.empty()) break;
    }
    return merged;
}

void SurfaceIndex::build(const std::vector<Surface>& surfaces, const RectF& bounds) {
    buckets_.clear();
    surfaceCount_ = surfaces.size();
    seen_.assign(surfaceCount_, 0);
    stamp_ = 0;
    if (bounds.empty()) return;
    origin_ = bounds.left;
    const auto count = static_cast<std::size_t>(std::ceil(bounds.width() / kBucketWidth)) + 1;
    buckets_.resize(count);
    for (std::size_t s = 0; s < surfaces.size(); ++s) {
        const Surface& surf = surfaces[s];
        if (surf.pile.size() == 0) continue;
        const int b0 = bucketOf(surf.rect.left);
        const int b1 = bucketOf(surf.rect.right);
        for (int b = b0; b <= b1; ++b) {
            buckets_[static_cast<std::size_t>(b)].push_back(static_cast<std::uint32_t>(s));
        }
    }
}

int SurfaceIndex::bucketOf(float x) const {
    const int b = static_cast<int>(std::floor((x - origin_) / kBucketWidth));
    return std::clamp(b, 0, static_cast<int>(buckets_.size()) - 1);
}


}  // namespace sim
