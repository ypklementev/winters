#include <cmath>
#include <vector>

#include "mini_test.h"
#include "sim/heightmap.h"

using sim::Heightmap;

TEST(heightmap_deposit_conserves_area) {
    Heightmap h(100, 2.0f);
    const float got = h.deposit(100.0f, 3.0f, 12.0f, 100.0f);
    CHECK_NEAR(got, 12.0f, 1e-3);
    CHECK_NEAR(h.totalArea(), 12.0f, 1e-3);
    // Peak is at the deposit point.
    CHECK(h.height(50) >= h.height(48));
}

TEST(heightmap_deposit_respects_max_height_and_mask) {
    Heightmap h(10, 2.0f);
    for (std::size_t i = 0; i < 10; ++i) h.setVisible(i, i >= 5);
    CHECK_NEAR(h.deposit(3.0f, 1.0f, 10.0f, 100.0f), 0.0f, 1e-6);  // all masked
    h.deposit(15.0f, 1.0f, 1000.0f, 4.0f);
    for (std::size_t i = 0; i < 10; ++i) {
        CHECK(h.height(i) <= 4.0f + 1e-5f);
        if (i < 5) CHECK(h.height(i) == 0.0f);
    }
}

TEST(heightmap_relax_limits_slope_and_conserves_inside) {
    Heightmap h(200, 2.0f);
    h.setHeight(100, 50.0f);
    std::vector<Heightmap::Spill> spills;
    const float before = h.totalArea();
    for (int i = 0; i < 2000; ++i) h.relax(0.7f, 2, spills);
    const float lim = 0.7f * 2.0f;
    for (std::size_t i = 0; i + 1 < h.size(); ++i) {
        CHECK(std::abs(h.height(i) - h.height(i + 1)) <= lim + 1e-3f);
    }
    // Pile is far from the ends: nothing spilled.
    CHECK(spills.empty());
    CHECK_NEAR(h.totalArea(), before, 1e-2);
}

TEST(heightmap_relax_spills_over_edges) {
    Heightmap h(10, 2.0f);
    for (std::size_t i = 0; i < 10; ++i) h.setHeight(i, 20.0f);
    std::vector<Heightmap::Spill> spills;
    const float before = h.totalArea();
    for (int i = 0; i < 500; ++i) h.relax(0.7f, 2, spills);
    float spilled = 0.0f;
    bool left = false, right = false;
    for (const auto& s : spills) {
        spilled += s.area;
        if (s.localX < 0.0f) left = true;
        if (s.localX > 20.0f) right = true;
    }
    CHECK(left && right);
    CHECK(spilled > 0.0f);
    CHECK_NEAR(h.totalArea() + spilled, before, 1e-2);
    // Edge cells end up low.
    CHECK(h.height(0) <= 2.0f * 0.7f * 2.0f + 1e-3f);
}

TEST(heightmap_relax_spills_into_masked_gap) {
    Heightmap h(20, 2.0f);
    for (std::size_t i = 0; i < 20; ++i) h.setHeight(i, 10.0f);
    for (std::size_t i = 8; i < 12; ++i) {
        h.setHeight(i, 0.0f);
        h.setVisible(i, false);
    }
    std::vector<Heightmap::Spill> spills;
    for (int i = 0; i < 200; ++i) h.relax(0.7f, 2, spills);
    bool gapSpill = false;
    for (const auto& s : spills) {
        if (s.localX > 14.0f && s.localX < 26.0f) gapSpill = true;
    }
    CHECK(gapSpill);
    for (std::size_t i = 8; i < 12; ++i) CHECK(h.height(i) == 0.0f);
}

TEST(heightmap_resample_preserves_area) {
    Heightmap h(100, 2.0f);
    for (std::size_t i = 0; i < 100; ++i) h.setHeight(i, static_cast<float>(i % 13));
    const float before = h.totalArea();
    h.resample(37);
    CHECK(h.size() == 37);
    CHECK_NEAR(h.totalArea(), before, before * 1e-4);
    h.resample(250);
    CHECK_NEAR(h.totalArea(), before, before * 1e-4);
}

TEST(heightmap_lower_to_returns_removed_area) {
    Heightmap h(4, 2.0f);
    h.setHeight(1, 10.0f);
    CHECK_NEAR(h.lowerTo(1, 4.0f), 12.0f, 1e-5);
    CHECK_NEAR(h.height(1), 4.0f, 1e-6);
    CHECK_NEAR(h.lowerTo(1, 8.0f), 0.0f, 1e-6);
    CHECK_NEAR(h.lowerTo(1, -5.0f), 8.0f, 1e-5);
}
