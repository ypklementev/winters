#include "mini_test.h"
#include "sim/settings_model.h"

using namespace sim;

TEST(settings_intensity_increases_snowfall) {
    UserSettings s;
    s.intensity = Intensity::Light;
    const float light = toSnowConfig(s).spawnRate;
    s.intensity = Intensity::Steady;
    const float steady = toSnowConfig(s).spawnRate;
    s.intensity = Intensity::Blizzard;
    const float blizzard = toSnowConfig(s).spawnRate;
    CHECK(light < steady && steady < blizzard);
}

TEST(settings_wind_direction_and_strength) {
    UserSettings s;
    CHECK(toSnowConfig(s).wind == 0.0f);
    s.windDirection = WindDirection::Left;
    s.windStrength = 3;
    const float strongLeft = toSnowConfig(s).wind;
    s.windStrength = 1;
    const float weakLeft = toSnowConfig(s).wind;
    CHECK(strongLeft < weakLeft && weakLeft < 0.0f);
    s.windDirection = WindDirection::Right;
    CHECK(toSnowConfig(s).wind > 0.0f);
}

TEST(settings_sanitize_clamps_garbage_from_ini) {
    UserSettings s;
    s.intensity = static_cast<Intensity>(42);
    s.windDirection = static_cast<WindDirection>(-7);
    s.windStrength = 99;
    const UserSettings c = sanitize(s);
    CHECK(c.intensity == Intensity::Blizzard);
    CHECK(c.windDirection == WindDirection::Calm);
    CHECK(c.windStrength == 3);
    CHECK(toSnowConfig(s).wind == 0.0f);
}

TEST(settings_unlimited_mode_maps_to_config) {
    UserSettings s;
    const SnowConfig normal = toSnowConfig(s);
    s.unlimited = true;
    const SnowConfig c = toSnowConfig(s);
    CHECK(c.unlimited);
    CHECK(!normal.unlimited);
    CHECK(c.maxFlakes > normal.maxFlakes);
}
