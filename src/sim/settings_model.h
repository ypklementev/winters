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
    bool autoPauseFullscreen = true;
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
    switch (s.intensity) {
        case Intensity::Light:
            c.spawnRate = 7.0f;
            c.maxFlakes = 3000;
            c.gustiness = 0.25f;
            break;
        case Intensity::Steady:
            c.spawnRate = 18.0f;
            c.maxFlakes = 6000;
            c.gustiness = 0.35f;
            break;
        case Intensity::Blizzard:
            c.spawnRate = 55.0f;
            c.maxFlakes = 12000;
            c.gustiness = 0.6f;
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
