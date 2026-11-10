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
    // Unhurried push along the pile (500 px/s) with Ctrl+Alt (plow mode).
    movePointer(w, {200, line + 4}, {350, line + 4}, 0.3f, false, true);
    REQUIRE(w.snowballs().size() >= 1);
    float r1 = 0.0f;
    for (const auto& b : w.snowballs()) r1 = std::max(r1, b.radius);
    movePointer(w, {350, line + 4}, {800, line + 4}, 0.9f, false, true);
    float r2 = 0.0f;
    for (const auto& b : w.snowballs()) r2 = std::max(r2, b.radius);
    CHECK(r2 > r1 * 1.2f);
    CHECK(w.snowballs().size() <= 2);  // one ball keeps growing, not a trail of them
}

TEST(plow_needs_ctrl_alt) {
    World w = snowyWindow(7);
    const float line = lineAt(w, 1, 300);
    movePointer(w, {200, line + 4}, {800, line + 4}, 1.2f, false);  // same push, no modifier
    CHECK(w.snowballs().empty());
}

TEST(plow_fast_flick_still_throws_snow) {
    World w = snowyWindow(4);
    const float line = lineAt(w, 1, 300);
    const std::size_t before = w.flakes().size();
    movePointer(w, {200, line + 4}, {900, line + 4}, 0.2f, false, true);  // 3500 px/s
    CHECK(w.snowballs().empty());
    CHECK(w.flakes().size() > before + 20);
}

TEST(snowball_can_be_dragged_without_sculpt_keys) {
    World w = snowyWindow(5);
    const float line = lineAt(w, 1, 300);
    movePointer(w, {200, line + 4}, {450, line + 4}, 0.5f, false, true);
    REQUIRE(!w.snowballs().empty());
    idlePointer(w, {10, 10}, 2.0f);  // let it settle
    const Snowball b = w.snowballs().back();
    CHECK(w.hitTest(b.pos) == HitKind::Snowball);
    movePointer(w, b.pos, b.pos, 0.05f, true);  // press on it (no Ctrl+Alt)
    CHECK(w.holdingSnowball());
    movePointer(w, b.pos, {1300, 330}, 0.5f, true);  // low enough not to shatter
    movePointer(w, {1300, 330}, {1300, 330}, 0.1f, false);
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

// --- sticking, shattering, snowman --------------------------------------------------

namespace {

// Grab ball `id` and drop it gently onto the top of ball `onto`.
void stackOnto(World& w, std::uint32_t id, std::uint32_t onto) {
    auto get = [&](std::uint32_t i) { return w.snowballs()[static_cast<std::size_t>(findBall(w.snowballs(), i))]; };
    const Snowball b = get(id);
    const Vec2 lifted{b.pos.x, 120.0f};
    movePointer(w, b.pos, b.pos, 0.05f, true);
    movePointer(w, b.pos, lifted, 0.3f, true);  // up off the pile first
    const Snowball base = get(onto);            // where the base is *now*
    const Vec2 over{base.pos.x, 120.0f};
    movePointer(w, lifted, over, 0.5f, true);
    const Snowball base2 = get(onto);
    const Vec2 above{base2.pos.x, base2.pos.y - base2.radius - get(id).radius - 4.0f};
    movePointer(w, over, above, 0.3f, true);
    movePointer(w, above, above, 0.2f, true);
    movePointer(w, above, above, 0.05f, false);
    idlePointer(w, {10, 10}, 1.5f);
}

// Three balls of the given radii sitting on the window edge (y=400).
std::vector<std::uint32_t> makeBalls(World& w, std::initializer_list<float> radii) {
    std::vector<std::uint32_t> ids;
    float x = 300.0f;
    for (float r : radii) {
        // Plow from a fresh spot until a ball of roughly this size exists.
        const float line = lineAt(w, 1, x);
        const std::size_t before = w.snowballs().size();
        float pushed = 0.0f;
        while (w.snowballs().size() == before || w.snowballs().back().radius < r) {
            movePointer(w, {x + pushed, line + 4}, {x + pushed + 4.0f, line + 4}, 0.02f, false, true);
            pushed += 4.0f;
            if (pushed > 700.0f) break;
        }
        idlePointer(w, {10, 10}, 3.0f);  // let it roll to a stop
        ids.push_back(w.snowballs().back().id);
        x += 380.0f;
    }
    return ids;
}

}  // namespace

TEST(snowballs_dropped_from_high_shatter_into_snow) {
    World w = snowyWindow(8);
    const auto ids = makeBalls(w, {14.0f});
    REQUIRE(ids.size() == 1);
    const Snowball b = w.snowballs().back();
    movePointer(w, b.pos, b.pos, 0.05f, true);
    movePointer(w, b.pos, {800, 20}, 0.4f, true);
    movePointer(w, {800, 20}, {800, 20}, 0.2f, true);  // hold still up high
    const std::size_t flakesBefore = w.flakes().size();
    movePointer(w, {800, 20}, {800, 20}, 0.05f, false);  // let go: ~380 px fall
    idlePointer(w, {10, 10}, 1.0f);
    CHECK(findBall(w.snowballs(), ids[0]) < 0);
    CHECK(w.flakes().size() > flakesBefore + 10);
}

TEST(snowballs_gently_stacked_stick_and_move_together) {
    World w = snowyWindow(9);
    const auto ids = makeBalls(w, {26.0f, 15.0f});
    REQUIRE(ids.size() == 2);
    stackOnto(w, ids[1], ids[0]);
    const auto& balls = w.snowballs();
    const int top = findBall(balls, ids[1]);
    REQUIRE(top >= 0 && findBall(balls, ids[0]) >= 0);
    CHECK(balls[static_cast<std::size_t>(top)].parent == ids[0]);
    // Drag the base: the top ball comes along.
    const Snowball base = balls[static_cast<std::size_t>(findBall(balls, ids[0]))];
    const Vec2 dest{base.pos.x + 200.0f, base.pos.y - 10.0f};
    movePointer(w, base.pos, base.pos, 0.05f, true);
    movePointer(w, base.pos, dest, 0.6f, true);
    const Snowball& b2 = w.snowballs()[static_cast<std::size_t>(findBall(w.snowballs(), ids[0]))];
    const Snowball& t2 = w.snowballs()[static_cast<std::size_t>(findBall(w.snowballs(), ids[1]))];
    CHECK(std::abs(t2.pos.x - b2.pos.x) < b2.radius * 0.6f);
    CHECK(t2.pos.y < b2.pos.y);
    movePointer(w, dest, dest, 0.05f, false);
}

TEST(snowman_is_recognised_large_to_small) {
    std::vector<Snowball> balls(3);
    balls[0].id = 1;
    balls[0].radius = 30;
    balls[0].pos = {100, 300};
    balls[1].id = 2;
    balls[1].parent = 1;
    balls[1].radius = 20;
    balls[1].pos = {102, 250};
    balls[2].id = 3;
    balls[2].parent = 2;
    balls[2].radius = 12;
    balls[2].pos = {101, 218};
    auto men = findSnowmen(balls);
    REQUIRE(men.size() == 1);
    CHECK(men[0].base == 0 && men[0].middle == 1 && men[0].head == 2);
    // Wrong order (head bigger than the middle): no snowman.
    balls[2].radius = 25;
    CHECK(findSnowmen(balls).empty());
    // Only two balls: no snowman.
    balls[2].radius = 12;
    balls[2].parent = 0;
    CHECK(findSnowmen(balls).empty());
}

TEST(snowman_built_by_hand_gets_decorated) {
    World w = snowyWindow(10);
    auto ids = makeBalls(w, {34.0f, 22.0f});
    REQUIRE(ids.size() == 2);
    // Base must be the bigger one.
    auto radiusOf = [&](std::uint32_t id) { return w.snowballs()[static_cast<std::size_t>(findBall(w.snowballs(), id))].radius; };
    if (radiusOf(ids[0]) < radiusOf(ids[1])) std::swap(ids[0], ids[1]);
    REQUIRE(radiusOf(ids[1]) < radiusOf(ids[0]) * 0.92f);
    stackOnto(w, ids[1], ids[0]);
    // Head: a small Ctrl+Alt snowball made right off the pile.
    const float x = 1350.0f;
    const float line = lineAt(w, 1, x);
    movePointer(w, {x, line - 2}, {x, line - 2}, 0.05f, true, true);
    REQUIRE(w.holdingSnowball());
    const std::uint32_t head = w.snowballs().back().id;
    REQUIRE(radiusOf(head) < radiusOf(ids[1]) * 0.92f);
    movePointer(w, {x, line - 2}, {x, line - 2}, 0.05f, false, false);
    idlePointer(w, {10, 10}, 1.0f);
    stackOnto(w, head, ids[1]);
    REQUIRE(w.snowmen().size() == 1);
    const Snowball& h = w.snowballs()[w.snowmen()[0].head];
    CHECK(h.id == head);
    CHECK(h.decoratedAt >= 0.0f);
    // Lifting the head off breaks the snowman.
    movePointer(w, h.pos, h.pos, 0.05f, true);
    CHECK(w.snowmen().empty());
    movePointer(w, {200, 200}, {200, 200}, 0.05f, true);
}
