// Cursor sweeping and sculpting snowballs.
#include "mini_test.h"
#include "test_helpers.h"

using namespace sim;
using th::makeMonitor;
using th::makeWindow;

namespace {

// Window 1 at y=300 with a decent pile on it, snowfall stopped.
World snowyWindow(std::uint64_t seed) {
    World w(seed);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1200, 800)});
    w.setWindows({makeWindow(1, 100, 300, 1100, 600)});
    th::runSeconds(w, 15.0f);
    SnowConfig cfg = w.config();
    cfg.spawnRate = 0.0f;
    w.setConfig(cfg);
    th::runSeconds(w, 20.0f);  // let the air clear
    return w;
}

void movePointer(World& w, Vec2 from, Vec2 to, float seconds, bool sculpt, bool pressed) {
    const int steps = static_cast<int>(seconds / kFixedDt);
    const Vec2 vel = (to - from) * (1.0f / seconds);
    for (int i = 0; i <= steps; ++i) {
        PointerInput p;
        p.valid = true;
        p.pos = from + (to - from) * (static_cast<float>(i) / static_cast<float>(steps));
        p.vel = vel;
        p.sculpt = sculpt;
        p.pressed = pressed;
        w.setPointer(p);
        w.step(kFixedDt);
    }
}

float pileHeightNear(const World& w, std::uint64_t id, float x) {
    const Surface* s = th::findSurface(w, id);
    return s->edgeY() - s->lineAt(x);
}

}  // namespace

TEST(mitten_sweeps_snow_off_the_edge) {
    World w = snowyWindow(1);
    REQUIRE(pileHeightNear(w, 1, 600) > 3.0f);
    const std::size_t flakesBefore = w.flakes().size();
    movePointer(w, {300, 295}, {900, 295}, 0.5f, false, false);
    CHECK(pileHeightNear(w, 1, 600) < 0.5f);
    CHECK(pileHeightNear(w, 1, 1050) > 1.0f);  // outside the path: untouched
    CHECK(w.flakes().size() > flakesBefore);
    // Swept snow flies roughly in the movement direction.
    double vx = 0;
    const auto& f = w.flakes();
    for (std::size_t i = 0; i < f.size(); ++i) vx += f.vx[i];
    CHECK(vx > 0.0);
}

TEST(mitten_slow_cursor_does_not_sweep) {
    World w = snowyWindow(2);
    const float before = pileHeightNear(w, 1, 600);
    movePointer(w, {590, 295}, {610, 295}, 2.0f, false, false);  // 10 px/s
    CHECK_NEAR(pileHeightNear(w, 1, 600), before, 0.5);
}

TEST(sculpt_rolling_grows_a_snowball) {
    World w = snowyWindow(3);
    const float line = th::findSurface(w, 1)->lineAt(300);
    movePointer(w, {300, line - 2}, {300, line - 2}, 0.05f, true, true);  // press on the pile
    REQUIRE(w.snowballs().size() == 1);
    REQUIRE(w.holdingSnowball());
    const float r0 = w.snowballs()[0].radius;
    movePointer(w, {300, line}, {900, line}, 1.5f, true, true);  // roll it along
    CHECK(w.snowballs()[0].radius > r0 * 1.5f);
    // Release: it falls and comes to rest on the window edge.
    movePointer(w, {900, line - 50}, {900, line - 50}, 0.05f, true, false);
    CHECK(!w.holdingSnowball());
    th::runSeconds(w, 3.0f);
    const Snowball& b = w.snowballs()[0];
    CHECK(length(b.vel) < 1.0f);
    CHECK(b.pos.y + b.radius <= 300.5f);
    CHECK(b.pos.y + b.radius > 300.0f - 40.0f);
}

TEST(sculpt_press_on_empty_space_makes_nothing) {
    World w = snowyWindow(4);
    movePointer(w, {600, 100}, {650, 100}, 0.3f, true, true);
    CHECK(w.snowballs().empty());
}

TEST(snowballs_stack_into_a_snowman) {
    World w(5);
    w.setMonitors({makeMonitor(0, 0, 1200, 800)});
    SnowConfig cfg;
    cfg.spawnRate = 0.0f;
    w.setConfig(cfg);
    // Craft balls directly through the public API: press on snow and roll.
    th::heavySnow(w);
    w.setWindows({});
    th::runSeconds(w, 20.0f);  // build ground snow
    cfg.spawnRate = 0.0f;
    w.setConfig(cfg);
    const auto groundLine = [&](float x) {
        for (const auto& s : w.surfaces())
            if (s.ground) return s.lineAt(x);
        return 800.0f;
    };
    // Big ball at x=300.
    movePointer(w, {200, groundLine(200) - 2}, {200, groundLine(200) - 2}, 0.05f, true, true);
    movePointer(w, {200, groundLine(200)}, {1100, groundLine(1100)}, 2.0f, true, true);
    movePointer(w, {300, 700}, {300, 700}, 0.05f, true, false);
    th::runSeconds(w, 3.0f);
    REQUIRE(w.snowballs().size() == 1);
    const Snowball base = w.snowballs()[0];
    // Small ball: grab from remaining snow and drop it on top of the base.
    const float x2 = 100.0f;
    movePointer(w, {x2, groundLine(x2) - 2}, {x2, groundLine(x2) - 2}, 0.05f, true, true);
    REQUIRE(w.snowballs().size() == 2);
    const float topY = base.pos.y - base.radius - w.snowballs()[1].radius - 10.0f;
    movePointer(w, {x2, groundLine(x2) - 30}, {base.pos.x, topY}, 0.6f, true, true);
    movePointer(w, {base.pos.x, topY}, {base.pos.x, topY}, 0.3f, true, true);  // hold still
    movePointer(w, {base.pos.x, topY}, {base.pos.x, topY}, 0.05f, true, false);
    th::runSeconds(w, 4.0f);
    REQUIRE(w.snowballs().size() == 2);
    const Snowball& lower = w.snowballs()[0].pos.y > w.snowballs()[1].pos.y ? w.snowballs()[0] : w.snowballs()[1];
    const Snowball& upper = &lower == &w.snowballs()[0] ? w.snowballs()[1] : w.snowballs()[0];
    CHECK(upper.pos.y < lower.pos.y - lower.radius * 0.8f);   // it's on top
    CHECK(std::abs(upper.pos.x - lower.pos.x) < lower.radius);  // and didn't roll off
    CHECK(length(upper.vel) < 1.0f);
}

TEST(snowball_contacts_resolve_overlap) {
    std::vector<Snowball> balls(2);
    balls[0].pos = {0, 0};
    balls[0].radius = 10;
    balls[1].pos = {15, 0};
    balls[1].radius = 10;
    balls[1].vel = {-50, 0};
    resolveBallContacts(balls, 4);
    CHECK(length(balls[1].pos - balls[0].pos) >= 19.9f);
    CHECK(balls[1].vel.x - balls[0].vel.x >= -1e-3f);  // no longer approaching
}

TEST(snowball_held_ball_is_immovable) {
    std::vector<Snowball> balls(2);
    balls[0].pos = {0, 0};
    balls[0].radius = 10;
    balls[0].held = true;
    balls[1].pos = {5, 0};
    balls[1].radius = 10;
    resolveBallContacts(balls, 4);
    CHECK(balls[0].pos.x == 0.0f && balls[0].pos.y == 0.0f);
    CHECK(balls[1].pos.x >= 19.9f);
}
