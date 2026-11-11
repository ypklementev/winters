// Snow lying on windows, taskbar and screen bottom; z-order; collapse.
#include "mini_test.h"
#include "test_helpers.h"

using namespace sim;
using th::makeMonitor;
using th::makeWindow;

TEST(piles_snow_accumulates_on_window_edge) {
    World w(1);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1000, 800)});
    w.setWindows({makeWindow(42, 200, 300, 700, 600)});
    th::runSeconds(w, 10.0f);
    const Surface* s = th::findSurface(w, 42);
    REQUIRE(s != nullptr);
    CHECK(s->pile.totalArea() > 500.0f);
    // Snow lies only along the window, nothing below its edge sticks to it.
    const auto& f = w.flakes();
    for (std::size_t i = 0; i < f.size(); ++i) CHECK(f.y[i] < 900.0f);
}

TEST(piles_ground_accumulates_and_taskbar_hides_it) {
    World w(2);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1000, 800), makeMonitor(1000, 0, 2000, 800)});
    // Taskbar only on the first monitor.
    w.setWindows({makeWindow(7, 0, 752, 1000, 800, true)});
    th::runSeconds(w, 45.0f);  // small flakes need ~40 s to reach y=800
    const Surface* tb = th::findSurface(w, 7);
    REQUIRE(tb != nullptr);
    CHECK(tb->pile.totalArea() > 100.0f);
    for (const auto& s : w.surfaces()) {
        if (!s.ground) continue;
        if (s.rect.left < 1000.0f) {
            CHECK(s.pile.totalArea() == 0.0f);  // under the taskbar
        } else {
            CHECK(s.pile.totalArea() > 100.0f);
        }
    }
}

TEST(piles_zorder_covered_part_gets_no_snow) {
    World w(3);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1000, 800)});
    // Window 1 is on top and covers the middle of window 2's top edge.
    w.setWindows({makeWindow(1, 400, 200, 600, 500), makeWindow(2, 100, 300, 900, 600)});
    th::runSeconds(w, 10.0f);
    const Surface* back = th::findSurface(w, 2);
    REQUIRE(back != nullptr);
    float covered = 0.0f, open = 0.0f;
    for (std::size_t i = 0; i < back->pile.size(); ++i) {
        const float x = back->cellCenterX(i);
        if (x > 400.0f && x < 600.0f) covered += back->pile.height(i);
        else open += back->pile.height(i);
    }
    CHECK(covered == 0.0f);
    CHECK(open > 0.0f);
    CHECK(th::findSurface(w, 1)->pile.totalArea() > 0.0f);
}

TEST(piles_moving_window_collapses_snow_into_particles) {
    World w(4);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1000, 800)});
    w.setWindows({makeWindow(9, 200, 300, 700, 600)});
    th::runSeconds(w, 8.0f);
    const float lying = th::findSurface(w, 9)->pile.totalArea();
    REQUIRE(lying > 200.0f);
    SnowConfig cfg = w.config();
    cfg.spawnRate = 0.0f;
    w.setConfig(cfg);
    const std::size_t flakesBefore = w.flakes().size();
    w.setWindows({makeWindow(9, 250, 380, 750, 680)});
    CHECK(th::findSurface(w, 9)->pile.totalArea() == 0.0f);
    CHECK(w.flakes().size() > flakesBefore);
    // The debris falls and lands on the screen bottom.
    th::runSeconds(w, 20.0f);
    CHECK(th::groundArea(w) > lying * 0.3f);
}

TEST(piles_closing_window_drops_snow) {
    World w(5);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1000, 800)});
    w.setWindows({makeWindow(9, 200, 300, 700, 600)});
    th::runSeconds(w, 8.0f);
    const std::size_t before = w.flakes().size();
    w.setWindows({});
    CHECK(th::findSurface(w, 9) == nullptr);
    CHECK(w.flakes().size() > before);
}

TEST(piles_covering_window_collapses_covered_part_only) {
    World w(6);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1000, 800)});
    w.setWindows({makeWindow(2, 100, 300, 900, 600)});
    th::runSeconds(w, 8.0f);
    const float before = th::findSurface(w, 2)->pile.totalArea();
    // Another window comes to front over the middle.
    w.setWindows({makeWindow(1, 400, 200, 600, 500), makeWindow(2, 100, 300, 900, 600)});
    const Surface* s = th::findSurface(w, 2);
    const float after = s->pile.totalArea();
    CHECK(after < before);
    CHECK(after > before * 0.5f);
    for (std::size_t i = 0; i < s->pile.size(); ++i) {
        const float x = s->cellCenterX(i);
        if (x > 400.0f && x < 600.0f) CHECK(s->pile.height(i) == 0.0f);
    }
}

TEST(piles_resize_rescales_preserving_snow) {
    World w(7);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1000, 800)});
    w.setWindows({makeWindow(3, 200, 300, 700, 600)});
    th::runSeconds(w, 8.0f);
    const float before = th::findSurface(w, 3)->pile.totalArea();
    REQUIRE(before > 0.0f);
    w.setWindows({makeWindow(3, 200, 300, 900, 600)});  // wider, same top-left
    const Surface* s = th::findSurface(w, 3);
    CHECK(s->pile.size() == 350);
    CHECK_NEAR(s->pile.totalArea(), before, before * 0.02);
}

TEST(piles_maximized_window_lets_snow_fall_to_taskbar) {
    World w(8);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1000, 800)});
    w.setWindows({makeWindow(5, 0, 752, 1000, 800, true), makeWindow(6, 0, 0, 1000, 752)});
    th::runSeconds(w, 45.0f);  // small flakes need ~40 s to reach y=800
    CHECK(th::findSurface(w, 6)->pile.totalArea() == 0.0f);
    CHECK(th::findSurface(w, 5)->pile.totalArea() > 100.0f);
}

TEST(piles_slope_is_bounded) {
    World w(9);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1000, 800)});
    w.setWindows({makeWindow(3, 300, 300, 400, 600)});
    th::runSeconds(w, 30.0f);
    const Surface* s = th::findSurface(w, 3);
    for (std::size_t i = 0; i + 1 < s->pile.size(); ++i) {
        CHECK(std::abs(s->pile.height(i) - s->pile.height(i + 1)) < 0.7f * 2.0f + 2.5f);
    }
    CHECK(s->pile.height(0) < 10.0f);  // edges taper off
}

TEST(piles_clear_removes_everything) {
    World w(10);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1000, 800)});
    w.setWindows({makeWindow(3, 200, 300, 700, 600)});
    th::runSeconds(w, 5.0f);
    w.clear();
    CHECK(w.lyingArea() == 0.0f);
    CHECK(w.flakes().size() == 0);
    CHECK(w.snowballs().empty());
}

TEST(piles_stacked_monitor_bottom_is_not_a_floor) {
    World w(11);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1000, 500), makeMonitor(0, 500, 1000, 1000)});
    th::runSeconds(w, 45.0f);  // small flakes need ~40 s to reach y=800
    for (const auto& s : w.surfaces()) {
        if (!s.ground) continue;
        if (s.edgeY() == 500.0f) CHECK(s.pile.totalArea() == 0.0f);
        if (s.edgeY() == 1000.0f) CHECK(s.pile.totalArea() > 0.0f);
    }
}
