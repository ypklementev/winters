#include <algorithm>

#include "mini_test.h"
#include "sim/flake_shapes.h"
#include "sim/flakes.h"
#include "sim/rng.h"
#include "sim/wind.h"
#include "sim/world.h"

using namespace sim;

namespace {

Monitor makeMonitor(float l, float t, float r, float b, float scale = 1.0f) {
    Monitor m;
    m.bounds = {l, t, r, b};
    m.work = m.bounds;
    m.scale = scale;
    return m;
}

void runSeconds(World& w, float seconds) {
    const int steps = static_cast<int>(seconds / kFixedDt);
    for (int i = 0; i < steps; ++i) w.step(kFixedDt);
}

}  // namespace

TEST(rng_uniform_in_range) {
    Rng r(42);
    for (int i = 0; i < 10000; ++i) {
        const float u = r.uniform();
        CHECK(u >= 0.0f && u < 1.0f);
        const float v = r.uniform(-3.0f, 5.0f);
        CHECK(v >= -3.0f && v < 5.0f);
    }
}

TEST(pool_add_remove_keeps_arrays_consistent) {
    FlakePool p(3);
    FlakeSpawn s;
    for (int i = 0; i < 3; ++i) {
        s.pos = {static_cast<float>(i), 0.0f};
        CHECK(p.add(s));
    }
    CHECK(p.full());
    CHECK(!p.add(s));
    p.remove(0);  // last (x=2) moves into slot 0
    REQUIRE(p.size() == 2);
    CHECK_NEAR(p.x[0], 2.0f, 1e-6);
    CHECK_NEAR(p.x[1], 1.0f, 1e-6);
    CHECK(p.radius.size() == 2 && p.alpha.size() == 2 && p.scale.size() == 2);
    p.clear();
    CHECK(p.size() == 0);
}

TEST(integrate_relaxes_to_terminal_velocity) {
    FlakePool p(1);
    FlakeSpawn s;
    s.vel = {500.0f, -300.0f};  // e.g. kicked by the cursor
    s.fallSpeed = 50.0f;
    s.flutterAmp = 0.0f;
    p.add(s);
    WindField air;
    air.configure({0, 0, 1000, 1000});
    air.update(0.0f, 20.0f, 0.0f);  // steady 20 px/s wind, no eddies
    for (int i = 0; i < 600; ++i) integrateFlakes(p, kFixedDt, &air);
    CHECK_NEAR(p.vx[0], 20.0f, 0.01);
    CHECK_NEAR(p.vy[0], 50.0f, 0.01);
}

TEST(world_spawns_and_flakes_fall) {
    World w(7);
    w.setMonitors({makeMonitor(0, 0, 1920, 1080)});
    runSeconds(w, 5.0f);
    REQUIRE(w.flakes().size() > 0);
    const auto& f = w.flakes();
    float maxY = 0;
    for (std::size_t i = 0; i < f.size(); ++i) {
        CHECK(f.x[i] > -100.0f && f.x[i] < 2020.0f);
        maxY = std::max(maxY, f.y[i]);
    }
    CHECK(maxY > 100.0f);  // they actually moved down
}

TEST(world_flakes_die_at_bottom_when_spawning_stops) {
    World w(7);
    w.setMonitors({makeMonitor(0, 0, 800, 600)});
    w.prewarm();
    CHECK(w.flakes().size() > 0);
    SnowConfig cfg = w.config();
    cfg.spawnRate = 0.0f;
    w.setConfig(cfg);
    runSeconds(w, 60.0f);
    CHECK(w.flakes().size() == 0);
}

TEST(world_is_deterministic_for_same_seed) {
    World a(123), b(123);
    for (World* w : {&a, &b}) {
        w->setMonitors({makeMonitor(0, 0, 1000, 800)});
        runSeconds(*w, 3.0f);
    }
    REQUIRE(a.flakes().size() == b.flakes().size());
    for (std::size_t i = 0; i < a.flakes().size(); ++i) {
        CHECK(a.flakes().x[i] == b.flakes().x[i]);
        CHECK(a.flakes().y[i] == b.flakes().y[i]);
    }
}

TEST(world_wind_pushes_flakes) {
    World w(9);
    w.setMonitors({makeMonitor(0, 0, 1000, 1000)});
    SnowConfig cfg;
    cfg.wind = 80.0f;
    w.setConfig(cfg);
    runSeconds(w, 10.0f);
    const auto& f = w.flakes();
    REQUIRE(f.size() > 50);
    double sum = 0;
    for (std::size_t i = 0; i < f.size(); ++i) sum += f.vx[i];
    CHECK(sum / f.size() > 60.0);
    // The upwind band means flakes exist left of the screen too.
    CHECK(*std::min_element(f.x.begin(), f.x.end()) < 0.0f);
}

TEST(world_respects_max_flakes) {
    World w(1);
    SnowConfig cfg;
    cfg.spawnRate = 5000.0f;
    cfg.maxFlakes = 300;
    w.setConfig(cfg);
    w.setMonitors({makeMonitor(0, 0, 3000, 2000)});
    runSeconds(w, 3.0f);
    CHECK(w.flakes().size() <= 300);
}

TEST(world_short_monitor_floor_culls_in_its_column) {
    // Left monitor is tall, right one is short (bottom-aligned at 500).
    World w(5);
    w.setMonitors({makeMonitor(0, 0, 1000, 1000), makeMonitor(1000, 0, 2000, 500)});
    runSeconds(w, 30.0f);
    const auto& f = w.flakes();
    REQUIRE(f.size() > 0);
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (f.x[i] >= 1000.0f && f.x[i] < 2000.0f) CHECK(f.y[i] - f.radius[i] <= 500.0f);
    }
}

TEST(world_stacked_monitors_do_not_double_spawn) {
    // Monitor B is directly below A: B must not spawn at its own top edge.
    World w(11);
    w.setMonitors({makeMonitor(0, 0, 1000, 500), makeMonitor(0, 500, 1000, 1000)});
    w.step(kFixedDt);
    w.step(kFixedDt);
    const auto& f = w.flakes();
    for (std::size_t i = 0; i < f.size(); ++i) CHECK(f.y[i] < 100.0f);
}

TEST(world_hidpi_monitor_gets_bigger_flakes) {
    auto meanRadius = [](float scale) {
        World w(3);
        w.setMonitors({makeMonitor(0, 0, 2000, 1000, scale)});
        w.prewarm();
        const auto& f = w.flakes();
        double sum = 0;
        for (std::size_t i = 0; i < f.size(); ++i) sum += f.radius[i];
        return f.size() ? sum / static_cast<double>(f.size()) : 0.0;
    };
    const double r1 = meanRadius(1.0f), r2 = meanRadius(2.0f);
    REQUIRE(r1 > 0.0);
    CHECK_NEAR(r2 / r1, 2.0, 0.1);
}

TEST(world_flakes_are_mostly_small_with_varied_shapes) {
    World w(4);
    w.setMonitors({makeMonitor(0, 0, 1920, 1080)});
    w.prewarm();
    const auto& f = w.flakes();
    REQUIRE(f.size() > 1000);
    std::size_t small = 0;
    bool shapes[4] = {};
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (f.radius[i] < 1.8f) ++small;
        shapes[static_cast<int>(shapeOfSprite(f.sprite[i]))] = true;
    }
    CHECK(small > f.size() * 85 / 100);
    CHECK(shapes[0] && shapes[1] && shapes[2] && shapes[3]);
}

TEST(wind_field_swirls_and_follows_base_wind) {
    WindField air;
    air.configure({0, 0, 2000, 1000});
    air.update(3.0f, 50.0f, 20.0f);
    double sumU = 0, sumV2 = 0;
    int n = 0;
    for (float y = 0; y < 1000; y += 37) {
        for (float x = 0; x < 2000; x += 41) {
            const Vec2 v = air.sample(x, y);
            sumU += v.x;
            sumV2 += v.y * v.y;
            ++n;
        }
    }
    CHECK_NEAR(sumU / n, 50.0, 8.0);  // eddies average out around the wind
    CHECK(sumV2 / n > 1.0);           // but there are up/down drafts
}

TEST(world_changing_monitors_culls_offscreen) {
    World w(3);
    w.setMonitors({makeMonitor(0, 0, 2000, 1000)});
    w.prewarm();
    w.setMonitors({makeMonitor(0, 0, 1000, 1000)});
    const auto& f = w.flakes();
    for (std::size_t i = 0; i < f.size(); ++i) CHECK(f.x[i] <= 1000.0f + 64.0f);
}
