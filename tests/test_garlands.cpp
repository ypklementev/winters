// Garland lights on window edges.
#include <cmath>

#include "mini_test.h"
#include "render/instances.h"
#include "render/shapes.h"
#include "sim/settings_model.h"
#include "test_helpers.h"

using namespace sim;
using th::makeMonitor;
using th::makeWindow;

namespace {

World lit(std::uint64_t seed, GarlandColors colors = GarlandColors::Multi, GarlandBlink blink = GarlandBlink::Steady) {
    World w(seed);
    SnowConfig cfg;
    cfg.spawnRate = 0.0f;
    w.setConfig(cfg);
    w.setGarlandConfig({colors, blink});
    w.setMonitors({makeMonitor(0, 0, 1600, 900)});
    return w;
}

float maxSwing(const World& w, std::uint64_t id) {
    float m = 0.0f;
    for (const Festoon& f : w.festoons()) {
        if (f.windowId == id) m = std::max(m, std::abs(f.swing));
    }
    return m;
}

}  // namespace

TEST(garlands_off_by_default) {
    World w(1);
    w.setMonitors({makeMonitor(0, 0, 1600, 900)});
    w.setWindows({makeWindow(1, 200, 300, 900, 700)});
    th::runSeconds(w, 0.5f);
    CHECK(w.festoons().empty());
}

TEST(garlands_hang_from_window_top_edges_only) {
    World w = lit(2);
    w.setWindows({makeWindow(1, 200, 300, 900, 700), makeWindow(7, 0, 852, 1600, 900, true)});
    th::runSeconds(w, 2.0f);
    REQUIRE(!w.festoons().empty());
    float covered = 0.0f;
    std::uint32_t expectBulb = 0;
    for (const Festoon& f : w.festoons()) {
        CHECK(f.windowId == 1);  // not on the taskbar
        CHECK(f.a.y == 300.0f && f.b.y == 300.0f);
        CHECK(f.a.x >= 200.0f && f.b.x <= 900.0f);
        CHECK(f.at(0.5f).y > 305.0f);  // sags down onto the window
        CHECK(f.bulbs >= 2);
        CHECK(f.firstBulb == expectBulb);  // numbered along the window
        expectBulb += static_cast<std::uint32_t>(f.bulbs);
        covered += f.b.x - f.a.x;
    }
    CHECK(covered > 690.0f);
    CHECK(maxSwing(w, 1) < 0.01f);  // calm, still window: hanging straight
}

TEST(garlands_skip_parts_covered_by_a_window_in_front) {
    World w = lit(3);
    // Window 2 in front starts just below window 1's edge: the edge is
    // visible, but lights hanging from it would be drawn over window 2.
    w.setWindows({makeWindow(2, 400, 305, 700, 800), makeWindow(1, 200, 300, 900, 700)});
    th::runSeconds(w, 0.5f);
    bool left = false, right = false;
    for (const Festoon& f : w.festoons()) {
        if (f.windowId != 1) continue;
        CHECK(f.b.x <= 400.0f + 1.0f || f.a.x >= 700.0f - 1.0f);
        left = left || f.b.x <= 401.0f;
        right = right || f.a.x >= 699.0f;
    }
    CHECK(left);
    CHECK(right);
}

TEST(garlands_swing_when_the_window_is_dragged_then_settle) {
    World w = lit(4);
    w.setWindows({makeWindow(1, 200, 300, 900, 700)});
    th::runSeconds(w, 0.5f);
    // Yank it 300 px to the right in a quarter of a second, then stop.
    float swingDuring = 0.0f;
    for (int i = 1; i <= 15; ++i) {
        const float dx = 20.0f * static_cast<float>(i);
        w.setWindows({makeWindow(1, 200 + dx, 300, 900 + dx, 700)});
        w.step(kFixedDt);
        swingDuring = std::max(swingDuring, maxSwing(w, 1));
    }
    th::runSeconds(w, 0.3f);
    swingDuring = std::max(swingDuring, maxSwing(w, 1));
    CHECK(swingDuring > 0.2f);
    th::runSeconds(w, 6.0f);
    CHECK(maxSwing(w, 1) < 0.02f);
}

TEST(garlands_lean_with_the_wind) {
    World w = lit(5);
    SnowConfig cfg = w.config();
    cfg.wind = 120.0f;
    cfg.gustiness = 0.0f;
    w.setConfig(cfg);
    w.setWindows({makeWindow(1, 200, 300, 900, 700)});
    th::runSeconds(w, 5.0f);
    REQUIRE(!w.festoons().empty());
    const Festoon& f = w.festoons().front();
    CHECK(f.swing > 0.1f);
    CHECK(f.at(0.5f).x > (f.a.x + f.b.x) * 0.5f);  // blown to the right
}

TEST(garlands_forget_closed_windows) {
    World w = lit(6);
    w.setWindows({makeWindow(1, 200, 300, 900, 700)});
    th::runSeconds(w, 0.5f);
    REQUIRE(!w.festoons().empty());
    w.setWindows({});
    th::runSeconds(w, 0.1f);
    CHECK(w.festoons().empty());
}

TEST(garlands_render_bulbs_and_glows_that_blink) {
    World w = lit(7, GarlandColors::Multi, GarlandBlink::Chase);
    w.setWindows({makeWindow(1, 200, 300, 900, 700)});
    th::runSeconds(w, 0.5f);
    std::size_t bulbs = 0;
    for (const Festoon& f : w.festoons()) bulbs += static_cast<std::size_t>(f.bulbs);
    REQUIRE(bulbs > 10);

    auto glowAlphas = [&] {
        std::vector<float> a;
        for (const shapes::Shape& s : shapes::Build(w)) {
            if (s.type == shapes::kGlow) a.push_back(s.alpha);
        }
        return a;
    };
    const std::vector<float> a0 = glowAlphas();
    CHECK(!a0.empty());
    CHECK(a0.size() <= bulbs);
    th::runSeconds(w, 0.4f);
    CHECK(glowAlphas() != a0);  // running lights move

    // Garlands off: nothing drawn.
    w.setGarlandConfig({});
    th::runSeconds(w, 0.1f);
    render::FrameInstances fi;
    fi.build(w);
    CHECK(fi.shapes.empty());
}

TEST(settings_garlands_map_and_sanitize) {
    UserSettings s;
    CHECK(!toGarlandConfig(s).enabled());
    s.garland = GarlandColors::Warm;
    s.garlandBlink = GarlandBlink::Chase;
    CHECK(toGarlandConfig(s).colors == GarlandColors::Warm);
    CHECK(toGarlandConfig(s).blink == GarlandBlink::Chase);
    s.garland = static_cast<GarlandColors>(17);
    s.garlandBlink = static_cast<GarlandBlink>(-3);
    CHECK(sanitize(s).garland == GarlandColors::Blue);
    CHECK(sanitize(s).garlandBlink == GarlandBlink::Steady);
}
