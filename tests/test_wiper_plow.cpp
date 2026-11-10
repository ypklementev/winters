// Pushing snow into balls with the cursor, dragging balls without Ctrl+Alt,
// and the windshield wiper.
#include "mini_test.h"
#include "sim/wiper.h"
#include "test_helpers.h"

using namespace sim;
using th::makeMonitor;
using th::makeWindow;

namespace {

World snowyWindow(std::uint64_t seed) {
    World w(seed);
    th::heavySnow(w);
    w.setMonitors({makeMonitor(0, 0, 1600, 900)});
    w.setWindows({makeWindow(1, 100, 400, 1500, 700)});
    th::runSeconds(w, 25.0f);
    SnowConfig cfg = w.config();
    cfg.spawnRate = 0.0f;
    w.setConfig(cfg);
    th::runSeconds(w, 30.0f);  // let the air clear
    return w;
}

void movePointer(World& w, Vec2 from, Vec2 to, float seconds, bool pressed, bool sculpt = false) {
    const int steps = std::max(1, static_cast<int>(seconds / kFixedDt));
    const Vec2 vel = (to - from) * (1.0f / seconds);
    for (int i = 0; i <= steps; ++i) {
        PointerInput p;
        p.valid = true;
        p.pos = from + (to - from) * (static_cast<float>(i) / static_cast<float>(steps));
        p.vel = vel;
        p.pressed = pressed;
        p.sculpt = sculpt;
        w.setPointer(p);
        w.step(kFixedDt);
    }
}

void idlePointer(World& w, Vec2 at, float seconds) { movePointer(w, at, at, seconds, false); }

float lineAt(const World& w, std::uint64_t id, float x) { return th::findSurface(w, id)->lineAt(x); }

}  // namespace

// --- wiper kinematics ---------------------------------------------------------------

TEST(wiper_sweeps_between_rest_and_far_angle_then_pauses) {
    float lo = 10.0f, hi = -10.0f;
    for (float t = 0.0f; t < Wiper::kWipeTime; t += 0.01f) {
        const float a = Wiper::angleAt(t);
        lo = std::min(lo, a);
        hi = std::max(hi, a);
    }
    CHECK_NEAR(lo, Wiper::kRestAngle, 1e-3);
    CHECK_NEAR(hi, Wiper::kFarAngle, 0.01);
    CHECK_NEAR(Wiper::angleAt(Wiper::kWipeTime + 0.5f), Wiper::kRestAngle, 1e-6);  // dwell
    CHECK_NEAR(Wiper::angleAt(Wiper::kWipeTime * 0.5f), Wiper::kFarAngle, 1e-3);
}

TEST(wiper_velocity_is_tangential_and_matches_rotation) {
    Wiper wp;
    wp.place({500, 500}, 300, 1);
    wp.setEnabled(true);
    for (int i = 0; i < 20; ++i) wp.step(kFixedDt);  // moving counter-clockwise (angle increasing)
    REQUIRE(wp.angularVelocity() > 0.0f);
    const Vec2 tip = wp.tip();
    const Vec2 v = wp.velocityAt(tip);
    CHECK_NEAR(dot(v, tip - wp.pivot()), 0.0, 1e-2);
    // Numerically: where will the tip be next step?
    const Vec2 predicted = tip + v * kFixedDt;
    wp.step(kFixedDt);
    CHECK(length(predicted - wp.tip()) < 3.0f);
}

TEST(wiper_swept_area_covers_the_blade_path_only) {
    Wiper wp;
    wp.place({500, 500}, 300, 1);
    wp.setEnabled(true);
    wp.step(0.3f);
    const float mid = (wp.prevAngle() + wp.angle()) * 0.5f;
    CHECK(wp.swept(wp.pivot() + Wiper::direction(mid) * 250.0f, 0.0f));
    CHECK(!wp.swept(wp.pivot() + Wiper::direction(mid) * 50.0f, 0.0f));   // inside the bare arm part
    CHECK(!wp.swept(wp.pivot() + Wiper::direction(mid) * 340.0f, 0.0f));  // beyond the tip
    CHECK(!wp.swept(wp.pivot() + Wiper::direction(wp.angle() + 0.5f) * 250.0f, 0.0f));  // not reached yet
}

// --- wiper in the world ---------------------------------------------------------

TEST(wiper_clears_snow_in_its_arc_and_throws_it) {
    World w = snowyWindow(1);
    // Pivot below the window edge (y=400): the blade arc crosses the edge.
    w.placeWiper({800, 560});
    w.setWiperEnabled(true);
    const float L = w.wiper().length();
    REQUIRE(L > 200.0f);
    const float inside = 800.0f;                      // straight above the pivot
    const float outside = 800.0f + L + 100.0f;        // beyond the arc
    const float hInside = 400.0f - lineAt(w, 1, inside);
    const float hOutside = 400.0f - lineAt(w, 1, outside);
    REQUIRE(hInside > 3.0f && hOutside > 3.0f);
    const std::size_t before = w.flakes().size();
    idlePointer(w, {10, 10}, Wiper::kWipeTime);
    CHECK(400.0f - lineAt(w, 1, inside) < 0.5f);
    CHECK_NEAR(400.0f - lineAt(w, 1, outside), hOutside, 1.0);
    CHECK(w.flakes().size() > before);
    // The wiped snow flies fast.
    float fastest = 0.0f;
    for (std::size_t i = 0; i < w.flakes().size(); ++i) {
        fastest = std::max(fastest, std::abs(w.flakes().vx[i]) + std::abs(w.flakes().vy[i]));
    }
    CHECK(fastest > 300.0f);
}

TEST(wiper_pivot_can_be_dragged) {
    World w = snowyWindow(2);
    w.placeWiper({800, 800});
    w.setWiperEnabled(true);
    CHECK(w.hitTest({800, 800}) == HitKind::WiperPivot);
    CHECK(w.hitTest({800, 700}) == HitKind::None);
    movePointer(w, {800, 800}, {800, 800}, 0.05f, true);
    CHECK(w.holdingSomething());
    movePointer(w, {800, 800}, {300, 850}, 0.5f, true);
    movePointer(w, {300, 850}, {300, 850}, 0.05f, false);
    CHECK(!w.holdingSomething());
    CHECK(w.takeWiperMoved());
    CHECK(!w.takeWiperMoved());
    CHECK_NEAR(w.wiper().pivot().x, 300.0, 1.0);
}

// --- plow: pushing snow makes balls ------------------------------------------------

TEST(plow_pushing_snow_rolls_it_into_a_growing_ball) {
    World w = snowyWindow(3);
    const float line = lineAt(w, 1, 300);
    REQUIRE(400.0f - line > 3.0f);
    // Unhurried push along the pile (500 px/s), no buttons, no Ctrl+Alt.
    movePointer(w, {200, line + 4}, {350, line + 4}, 0.3f, false);
    REQUIRE(w.snowballs().size() >= 1);
    float r1 = 0.0f;
    for (const auto& b : w.snowballs()) r1 = std::max(r1, b.radius);
    movePointer(w, {350, line + 4}, {800, line + 4}, 0.9f, false);
    float r2 = 0.0f;
    for (const auto& b : w.snowballs()) r2 = std::max(r2, b.radius);
    CHECK(r2 > r1 * 1.2f);
    CHECK(w.snowballs().size() <= 2);  // one ball keeps growing, not a trail of them
}

TEST(plow_fast_flick_still_throws_snow) {
    World w = snowyWindow(4);
    const float line = lineAt(w, 1, 300);
    const std::size_t before = w.flakes().size();
    movePointer(w, {200, line + 4}, {900, line + 4}, 0.2f, false);  // 3500 px/s
    CHECK(w.snowballs().empty());
    CHECK(w.flakes().size() > before + 20);
}

TEST(snowball_can_be_dragged_without_sculpt_keys) {
    World w = snowyWindow(5);
    const float line = lineAt(w, 1, 300);
    movePointer(w, {200, line + 4}, {700, line + 4}, 1.0f, false);
    REQUIRE(!w.snowballs().empty());
    idlePointer(w, {10, 10}, 2.0f);  // let it settle
    const Snowball b = w.snowballs().back();
    CHECK(w.hitTest(b.pos) == HitKind::Snowball);
    movePointer(w, b.pos, b.pos, 0.05f, true);  // press on it (no Ctrl+Alt)
    CHECK(w.holdingSnowball());
    movePointer(w, b.pos, {1300, 200}, 0.5f, true);
    movePointer(w, {1300, 200}, {1300, 200}, 0.1f, false);
    idlePointer(w, {10, 10}, 3.0f);
    CHECK(!w.holdingSnowball());
    bool landedThere = false;
    for (const auto& s : w.snowballs()) {
        if (std::abs(s.pos.x - 1300.0f) < 30.0f && s.pos.y + s.radius <= 400.5f) landedThere = true;
    }
    CHECK(landedThere);  // dropped onto the window edge near x = 1300
}

TEST(pressing_on_empty_space_grabs_nothing) {
    World w = snowyWindow(6);
    movePointer(w, {800, 100}, {850, 120}, 0.3f, true);
    CHECK(!w.holdingSomething());
    CHECK(w.snowballs().empty());
}
