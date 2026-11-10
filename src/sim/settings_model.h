// User-facing settings and their mapping to simulation parameters.
// Pure data: persistence lives in app/settings.cpp.
#pragma once

#include <algorithm>

#include "sim/world.h"

namespace sim {

enum class Intensity { Light = 0, Steady = 1, Blizzard = 2 };
enum class WindDirection { Calm = 0, Left = 1, Right = 2 };

struct UserSettings {
    Intensity intensity = Intensity::Steady;
    WindDirection windDirection = WindDirection::Calm;
    int windStrength = 2;  // 1 weak, 2 medium, 3 strong
};

inline UserSettings sanitize(UserSettings s) {
    s.intensity = static_cast<Intensity>(std::clamp(static_cast<int>(s.intensity), 0, 2));
    s.windDirection = static_cast<WindDirection>(std::clamp(static_cast<int>(s.windDirection), 0, 2));
    s.windStrength = std::clamp(s.windStrength, 1, 3);
    return s;
}

inline SnowConfig toSnowConfig(const UserSettings& in) {
    const UserSettings s = sanitize(in);
    SnowConfig c;
    // spawnRate: flakes/s per 1000 logical px of width. flakeMass is tuned so
    // piles grow ~1 / 2.5 / 6 px per minute in the open.
    switch (s.intensity) {
        case Intensity::Light:
            c.spawnRate = 50.0f;
            c.maxFlakes = 6000;
            c.gustiness = 0.25f;
            c.turbulence = 10.0f;
            c.flakeMass = 0.3f;
            break;
        case Intensity::Steady:
            c.spawnRate = 140.0f;
            c.maxFlakes = 16000;
            c.gustiness = 0.35f;
            c.turbulence = 14.0f;
            c.flakeMass = 0.28f;
            break;
        case Intensity::Blizzard:
            c.spawnRate = 380.0f;
            c.maxFlakes = 40000;
            c.gustiness = 0.6f;
            c.turbulence = 30.0f;
            c.flakeMass = 0.24f;
            break;
    }
    static constexpr float kStrength[] = {0.0f, 25.0f, 60.0f, 120.0f};
    const float speed = kStrength[s.windStrength];
    c.wind = s.windDirection == WindDirection::Left    ? -speed
             : s.windDirection == WindDirection::Right ? speed
                                                       : 0.0f;
    return c;
}

}  // namespace sim
