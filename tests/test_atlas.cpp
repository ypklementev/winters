#include "mini_test.h"
#include "render/flake_atlas.h"
#include "sim/flake_shapes.h"

namespace {

struct CellStats {
    double coverage = 0;  // mean alpha 0..1
    double cx = 0, cy = 0;  // alpha-weighted centroid, in cell px
    int borderMax = 0;      // brightest pixel on the cell border
};

CellStats stats(const atlas::Image& img, int cell, int sprite) {
    CellStats s;
    const int ox = (sprite % sim::kAtlasGrid) * cell, oy = (sprite / sim::kAtlasGrid) * cell;
    double sum = 0;
    for (int y = 0; y < cell; ++y) {
        for (int x = 0; x < cell; ++x) {
            const int a = img.pixels[static_cast<size_t>(oy + y) * img.width + ox + x];
            sum += a;
            s.cx += a * (x + 0.5);
            s.cy += a * (y + 0.5);
            if (x == 0 || y == 0 || x == cell - 1 || y == cell - 1) s.borderMax = std::max(s.borderMax, a);
        }
    }
    s.coverage = sum / 255.0 / (cell * cell);
    if (sum > 0) {
        s.cx /= sum;
        s.cy /= sum;
    }
    return s;
}

}  // namespace

TEST(atlas_every_sprite_is_drawn_and_contained) {
    const int cell = 64;
    const atlas::Image img = atlas::GenerateFlakeAtlas(cell);
    REQUIRE(img.width == cell * sim::kAtlasGrid && img.height == img.width);
    for (int s = 0; s < sim::kSpriteCount; ++s) {
        const CellStats st = stats(img, cell, s);
        CHECK(st.coverage > 0.03);   // something is there
        CHECK(st.coverage < 0.7);    // and it isn't a filled square
        CHECK(st.borderMax == 0);    // nothing touches the cell edge (no bleeding)
    }
}

TEST(atlas_symmetric_crystals_are_centred) {
    const int cell = 64;
    const atlas::Image img = atlas::GenerateFlakeAtlas(cell);
    const auto r = sim::kSpriteRanges[static_cast<int>(sim::FlakeShape::Crystal)];
    for (int s = r.first; s < r.first + r.count; ++s) {
        const CellStats st = stats(img, cell, s);
        CHECK_NEAR(st.cx, cell / 2.0, 2.5);
        CHECK_NEAR(st.cy, cell / 2.0, 2.5);
    }
}

TEST(atlas_mips_halve_and_keep_coverage) {
    const int cell = 64;
    const atlas::Image img = atlas::GenerateFlakeAtlas(cell);
    const auto mips = atlas::BuildMips(img, cell);
    REQUIRE(mips.size() == 5);  // 64,32,16,8,4 px cells
    double base = 0, last = 0;
    for (auto p : mips.front().pixels) base += p;
    for (auto p : mips.back().pixels) last += p;
    CHECK(mips.back().width == img.width / 16);
    CHECK_NEAR(last * 256.0 / base, 1.0, 0.02);  // box filter preserves mean
}

TEST(atlas_is_deterministic) {
    CHECK(atlas::GenerateFlakeAtlas(32).pixels == atlas::GenerateFlakeAtlas(32).pixels);
}
