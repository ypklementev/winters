// render::FrameInstances: the per-frame GPU data both renderers draw.
#include "mini_test.h"
#include "render/instances.h"
#include "test_helpers.h"

TEST(instances_mirror_the_world) {
    sim::World w(3);
    w.setMonitors({th::makeMonitor(0, 0, 1600, 900)});
    w.setWindows({th::makeWindow(1, 200, 300, 900, 700)});
    w.prewarm();
    th::runSeconds(w, 20.0f);

    render::FrameInstances f;
    f.build(w);
    CHECK(f.flakes.size() == w.flakes().size());
    REQUIRE(!f.flakes.empty());
    for (const render::FlakeInstance& fl : f.flakes) {
        // Sub-pixel flakes are drawn 1 px wide and faded instead.
        CHECK(fl.radius >= 1.0f);
        CHECK(fl.alpha >= 0.0f && fl.alpha <= 1.0f);
        CHECK(fl.gain >= 1.0f && fl.gain <= 1.8f);
    }

    // Snow lies on the window and the screen bottom; every pile cell sits on a surface edge.
    REQUIRE(!f.piles.empty());
    bool onWindow = false, onGround = false;
    for (const render::PileInstance& p : f.piles) {
        CHECK(p.x1 > p.x0);
        CHECK(p.hLeft >= 0.0f && p.hRight >= 0.0f);
        onWindow = onWindow || (p.base == 300.0f && p.x0 >= 200.0f && p.x1 <= 900.0f);
        onGround = onGround || p.base == 900.0f;
    }
    CHECK(onWindow);
    CHECK(onGround);
    CHECK(f.balls.empty());
    CHECK(f.shapes.empty());  // no wiper, no snowmen
}

TEST(instances_draw_lower_balls_first_and_the_wiper) {
    sim::World w(5);
    w.setMonitors({th::makeMonitor(0, 0, 1600, 900)});
    w.setWindows({th::makeWindow(1, 200, 300, 900, 700)});
    w.prewarm();
    th::runSeconds(w, 30.0f);
    // Sculpt along the window's pile, then along the ground: two snowballs.
    for (float y : {298.0f, 898.0f}) {
        for (int i = 0; i < 90; ++i) {
            sim::PointerInput p;
            p.valid = true;
            p.sculpt = true;
            p.pos = {250.0f + 5.0f * static_cast<float>(i), y};
            p.vel = {300.0f, 0.0f};
            w.setPointer(p);
            w.step(sim::kFixedDt);
        }
        w.setPointer({});
        th::runSeconds(w, 0.5f);
    }
    w.setWiperEnabled(true);
    w.placeWiper({800.0f, 880.0f});

    render::FrameInstances f;
    f.build(w);
    REQUIRE(f.balls.size() >= 2);
    CHECK(f.balls.size() == w.snowballs().size());
    for (size_t i = 1; i < f.balls.size(); ++i) CHECK(f.balls[i - 1].y >= f.balls[i].y);
    CHECK(!f.shapes.empty());
    for (const render::ShapeInstance& s : f.shapes) CHECK(s.type == 0.0f || s.type == 1.0f || s.type == 2.0f);
}
