#include "render/flake_atlas.h"

#include <algorithm>
#include <cmath>

#include "sim/flake_shapes.h"
#include "sim/rng.h"

namespace atlas {

namespace {

constexpr float kPi = 3.14159265358979f;

struct Seg {
    float ax, ay, bx, by;
    float w0, w1;  // half-width at a and b (tapering arms)
};

float segCoverage(float px, float py, const Seg& s) {
    const float dx = s.bx - s.ax, dy = s.by - s.ay;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((px - s.ax) * dx + (py - s.ay) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float ex = s.ax + t * dx - px, ey = s.ay + t * dy - py;
    const float d = std::sqrt(ex * ex + ey * ey);
    const float w = s.w0 + (s.w1 - s.w0) * t;
    return std::clamp(w + 0.5f - d, 0.0f, 1.0f);
}

// A sprite under construction: float coverage, max-combined.
class Canvas {
public:
    explicit Canvas(int n) : n_(n), c_(static_cast<std::size_t>(n) * n, 0.0f) {}
    int size() const { return n_; }
    float& at(int x, int y) { return c_[static_cast<std::size_t>(y) * n_ + x]; }
    float at(int x, int y) const { return c_[static_cast<std::size_t>(y) * n_ + x]; }

    void segments(const std::vector<Seg>& segs, float opacity = 1.0f) {
        for (const Seg& s : segs) {
            const float pad = std::max(s.w0, s.w1) + 1.0f;
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min(s.ax, s.bx) - pad)));
            const int x1 = std::min(n_ - 1, static_cast<int>(std::ceil(std::max(s.ax, s.bx) + pad)));
            const int y0 = std::max(0, static_cast<int>(std::floor(std::min(s.ay, s.by) - pad)));
            const int y1 = std::min(n_ - 1, static_cast<int>(std::ceil(std::max(s.ay, s.by) + pad)));
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float c = segCoverage(x + 0.5f, y + 0.5f, s) * opacity;
                    at(x, y) = std::max(at(x, y), c);
                }
            }
        }
    }

    // Soft disc (used for grains and plate centres).
    void blob(float cx, float cy, float r, float opacity, float softness = 1.0f) {
        const int x0 = std::max(0, static_cast<int>(cx - r - softness - 1));
        const int x1 = std::min(n_ - 1, static_cast<int>(cx + r + softness + 1));
        const int y0 = std::max(0, static_cast<int>(cy - r - softness - 1));
        const int y1 = std::min(n_ - 1, static_cast<int>(cy + r + softness + 1));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
                const float c = std::clamp((r + softness * 0.5f - d) / softness, 0.0f, 1.0f) * opacity;
                at(x, y) = std::max(at(x, y), c);
            }
        }
    }

    // Filled convex polygon with anti-aliased edges (via signed distances).
    void polygon(const std::vector<float>& xs, const std::vector<float>& ys, float opacity) {
        const std::size_t k = xs.size();
        for (int y = 0; y < n_; ++y) {
            for (int x = 0; x < n_; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                // Signed distance to each edge line; works for either winding.
                float minPos = 1e9f, minNeg = 1e9f;
                for (std::size_t i = 0; i < k; ++i) {
                    const std::size_t j = (i + 1) % k;
                    const float ex = xs[j] - xs[i], ey = ys[j] - ys[i];
                    const float len = std::sqrt(ex * ex + ey * ey);
                    const float d = ((px - xs[i]) * ey - (py - ys[i]) * ex) / len;
                    minPos = std::min(minPos, d);
                    minNeg = std::min(minNeg, -d);
                }
                const float inside = std::max(minPos, minNeg);
                const float c = std::clamp(inside + 0.5f, 0.0f, 1.0f) * opacity;
                at(x, y) = std::max(at(x, y), c);
            }
        }
    }

private:
    int n_;
    std::vector<float> c_;
};

// --- shapes (centred at c, outer radius R) ---------------------------------

void drawGrain(Canvas& cv, sim::Rng& rng, float c, float R) {
    // A few overlapping soft lumps: an irregular rimed grain.
    const int lumps = 3 + static_cast<int>(rng.uniform() * 4.0f);
    for (int i = 0; i < lumps; ++i) {
        const float a = rng.uniform(0.0f, 2.0f * kPi);
        const float d = rng.uniform(0.0f, 0.35f) * R;
        const float r = rng.uniform(0.28f, 0.5f) * R;
        cv.blob(c + std::cos(a) * d, c + std::sin(a) * d, r, rng.uniform(0.75f, 1.0f), R * 0.25f);
    }
}

std::vector<Seg> dendriteSegments(sim::Rng& rng, float cx, float cy, float R, float rot, float thickness) {
    std::vector<Seg> segs;
    // One branch pattern shared by all six arms (crystals grow symmetrically),
    // each arm with slight imperfections.
    struct Branch {
        float t, len, angle;
    };
    std::vector<Branch> pattern;
    const int branches = 2 + static_cast<int>(rng.uniform() * 3.0f);
    for (int b = 0; b < branches; ++b) {
        const float t = 0.28f + 0.6f * (static_cast<float>(b) + rng.uniform(0.2f, 0.8f)) / static_cast<float>(branches);
        pattern.push_back({t, (1.0f - t) * rng.uniform(0.45f, 0.9f), kPi / 3.0f * rng.uniform(0.85f, 1.1f)});
    }
    const bool subBranches = rng.uniform() < 0.4f;
    for (int arm = 0; arm < 6; ++arm) {
        const float a = rot + static_cast<float>(arm) * kPi / 3.0f + rng.uniform(-0.04f, 0.04f);
        const float len = R * rng.uniform(0.88f, 1.0f);
        const float dx = std::cos(a), dy = std::sin(a);
        segs.push_back({cx, cy, cx + dx * len, cy + dy * len, thickness, thickness * 0.45f});
        for (const Branch& b : pattern) {
            const float bx = cx + dx * len * b.t, by = cy + dy * len * b.t;
            const float bl = len * b.len * rng.uniform(0.85f, 1.1f);
            for (float side : {-1.0f, 1.0f}) {
                const float ba = a + side * b.angle;
                const float ex = bx + std::cos(ba) * bl, ey = by + std::sin(ba) * bl;
                segs.push_back({bx, by, ex, ey, thickness * 0.75f, thickness * 0.35f});
                if (subBranches && bl > R * 0.25f) {
                    const float sx = bx + std::cos(ba) * bl * 0.5f, sy = by + std::sin(ba) * bl * 0.5f;
                    const float sa = ba - side * kPi / 3.0f;
                    segs.push_back({sx, sy, sx + std::cos(sa) * bl * 0.35f, sy + std::sin(sa) * bl * 0.35f,
                                    thickness * 0.5f, thickness * 0.3f});
                }
            }
        }
    }
    return segs;
}

void drawDendrite(Canvas& cv, sim::Rng& rng, float c, float R) {
    const float thick = R * rng.uniform(0.045f, 0.075f);
    cv.segments(dendriteSegments(rng, c, c, R, rng.uniform(0.0f, kPi), thick));
    // Small hexagonal hub.
    cv.blob(c, c, R * rng.uniform(0.08f, 0.16f), 1.0f, 1.0f);
}

void drawPlate(Canvas& cv, sim::Rng& rng, float c, float R) {
    const float rot = rng.uniform(0.0f, kPi);
    const float pr = R * rng.uniform(0.55f, 0.8f);
    std::vector<float> xs, ys;
    for (int k = 0; k < 6; ++k) {
        // Counter-clockwise on screen (y down): decreasing angle.
        const float a = rot - static_cast<float>(k) * kPi / 3.0f;
        xs.push_back(c + std::cos(a) * pr);
        ys.push_back(c + std::sin(a) * pr);
    }
    cv.polygon(xs, ys, rng.uniform(0.55f, 0.7f));  // translucent ice
    // Bright rim, faint ridges towards the corners, sometimes an inner ring.
    std::vector<Seg> rim, ribs;
    const float w = R * 0.035f;
    const bool ring = rng.uniform() < 0.5f;
    for (int k = 0; k < 6; ++k) {
        const int j = (k + 1) % 6;
        rim.push_back({xs[k], ys[k], xs[j], ys[j], w, w});
        ribs.push_back({c, c, xs[k], ys[k], w * 0.6f, w * 0.35f});
        if (ring) {
            const float f = 0.5f;
            ribs.push_back({c + (xs[k] - c) * f, c + (ys[k] - c) * f, c + (xs[j] - c) * f, c + (ys[j] - c) * f,
                            w * 0.5f, w * 0.5f});
        }
    }
    cv.segments(rim);
    cv.segments(ribs, 0.85f);
    // Sectored plates sprout stubby arms.
    if (rng.uniform() < 0.5f) {
        std::vector<Seg> arms;
        for (int k = 0; k < 6; ++k) {
            const float a = rot - static_cast<float>(k) * kPi / 3.0f;
            arms.push_back({xs[k], ys[k], c + std::cos(a) * R, c + std::sin(a) * R, w * 1.6f, w * 0.8f});
        }
        cv.segments(arms);
    }
}

void drawClump(Canvas& cv, sim::Rng& rng, float c, float R) {
    // Several small crystals and grains stuck together: deliberately lopsided.
    const int parts = 3 + static_cast<int>(rng.uniform() * 3.0f);
    for (int i = 0; i < parts; ++i) {
        const float a = rng.uniform(0.0f, 2.0f * kPi);
        const float d = rng.uniform(0.05f, 0.32f) * R;
        const float px = c + std::cos(a) * d, py = c + std::sin(a) * d;
        const float pr = R * rng.uniform(0.38f, 0.6f);
        if (rng.uniform() < 0.6f) {
            cv.segments(dendriteSegments(rng, px, py, pr, rng.uniform(0.0f, kPi), pr * 0.09f));
        } else {
            cv.blob(px, py, pr * 0.45f, rng.uniform(0.8f, 1.0f), pr * 0.2f);
        }
    }
}

}  // namespace

Image GenerateFlakeAtlas(int cellSize, std::uint32_t seed) {
    const int grid = sim::kAtlasGrid;
    Image img;
    img.width = img.height = cellSize * grid;
    img.pixels.assign(static_cast<std::size_t>(img.width) * img.height, 0);
    sim::Rng rng(seed);

    const float c = cellSize * 0.5f;
    const float R = cellSize * 0.44f;  // leaves a margin for mips and AA
    for (int s = 0; s < sim::kSpriteCount; ++s) {
        Canvas cv(cellSize);
        switch (sim::shapeOfSprite(s)) {
            case sim::FlakeShape::Speck:
                drawGrain(cv, rng, c, R * rng.uniform(0.7f, 1.0f));
                break;
            case sim::FlakeShape::Crystal:
                drawDendrite(cv, rng, c, R);
                break;
            case sim::FlakeShape::Plate:
                drawPlate(cv, rng, c, R);
                break;
            case sim::FlakeShape::Clump:
                drawClump(cv, rng, c, R);
                break;
        }
        const int ox = (s % grid) * cellSize, oy = (s / grid) * cellSize;
        for (int y = 0; y < cellSize; ++y) {
            for (int x = 0; x < cellSize; ++x) {
                const float v = std::clamp(cv.at(x, y), 0.0f, 1.0f);
                img.pixels[static_cast<std::size_t>(oy + y) * img.width + ox + x] =
                    static_cast<std::uint8_t>(v * 255.0f + 0.5f);
            }
        }
    }
    return img;
}

std::vector<Image> BuildMips(const Image& base, int cellSize, int minCellSize) {
    std::vector<Image> mips{base};
    int cell = cellSize;
    while (cell > minCellSize && mips.back().width > 1) {
        const Image& src = mips.back();
        Image dst;
        dst.width = src.width / 2;
        dst.height = src.height / 2;
        dst.pixels.resize(static_cast<std::size_t>(dst.width) * dst.height);
        for (int y = 0; y < dst.height; ++y) {
            for (int x = 0; x < dst.width; ++x) {
                const auto at = [&](int sx, int sy) {
                    return static_cast<int>(src.pixels[static_cast<std::size_t>(sy) * src.width + sx]);
                };
                const int sum = at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1);
                dst.pixels[static_cast<std::size_t>(y) * dst.width + x] = static_cast<std::uint8_t>((sum + 2) / 4);
            }
        }
        mips.push_back(std::move(dst));
        cell /= 2;
    }
    return mips;
}

}  // namespace atlas
