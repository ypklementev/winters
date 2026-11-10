#pragma once

#include "sim/world.h"

namespace th {

inline sim::Monitor makeMonitor(float l, float t, float r, float b, float scale = 1.0f) {
    sim::Monitor m;
    m.bounds = {l, t, r, b};
    m.work = m.bounds;
    m.scale = scale;
    return m;
}

inline sim::WindowInfo makeWindow(std::uint64_t id, float l, float t, float r, float b, bool taskbar = false) {
    sim::WindowInfo w;
    w.id = id;
    w.rect = {l, t, r, b};
    w.taskbar = taskbar;
    return w;
}

inline void runSeconds(sim::World& w, float seconds) {
    const int steps = static_cast<int>(seconds / sim::kFixedDt);
    for (int i = 0; i < steps; ++i) w.step(sim::kFixedDt);
}

inline const sim::Surface* findSurface(const sim::World& w, std::uint64_t id) {
    for (const auto& s : w.surfaces()) {
        if (!s.ground && s.id == id) return &s;
    }
    return nullptr;
}

inline float groundArea(const sim::World& w) {
    float a = 0.0f;
    for (const auto& s : w.surfaces()) {
        if (s.ground) a += s.pile.totalArea();
    }
    return a;
}

// Heavy snowfall without wind, for fast accumulation in tests.
inline void heavySnow(sim::World& w) {
    sim::SnowConfig cfg;
    cfg.spawnRate = 400.0f;
    cfg.maxFlakes = 20000;
    cfg.flakeMass = 1.5f;
    w.setConfig(cfg);
}

}  // namespace th
