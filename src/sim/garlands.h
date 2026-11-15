// Christmas lights hanging from the top edges of windows: festoons of wire
// sagging between anchor points, with bulbs along them. Each window's lights
// swing as one pendulum, pushed by the wind and by the window being dragged.
// The cursor doesn't touch them. Colours and blinking are the renderer's job
// (render/shapes.cpp); this only knows where the wire and bulbs are.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "sim/surfaces.h"
#include "sim/types.h"

namespace sim {

enum class GarlandColors { Off = 0, Multi = 1, Warm = 2, Blue = 3 };
enum class GarlandBlink { Steady = 0, Twinkle = 1, Chase = 2 };

struct GarlandConfig {
    GarlandColors colors = GarlandColors::Off;
    GarlandBlink blink = GarlandBlink::Steady;
    bool enabled() const { return colors != GarlandColors::Off; }
};

// One sagging span of wire between two anchors on a window's top edge.
// Point at u in [0, 1]: a + (b - a) * u + sag * 4u(1 - u).
struct Festoon {
    Vec2 a, b;
    Vec2 sag;                 // lowest point's offset from the chord midpoint (swung)
    float scale = 1.0f;
    float swing = 0.0f;       // rad, the window's pendulum angle (0 = hanging straight)
    std::uint64_t windowId = 0;
    std::uint32_t firstBulb = 0;  // index of its first bulb along the whole window
    int bulbs = 0;            // at u = (k + 0.5) / bulbs

    Vec2 at(float u) const { return a + (b - a) * u + sag * (4.0f * u * (1.0f - u)); }
};

class Garlands {
public:
    // Logical px (times the surface scale).
    static constexpr float kSpan = 96.0f;       // target festoon width
    static constexpr float kMinRun = 36.0f;     // shorter visible stretches get none
    static constexpr float kSag = 11.0f;        // depth of the dip
    static constexpr float kBulbSpacing = 21.0f;
    static constexpr float kHang = 15.0f;       // how far below the wire a bulb can reach

    // Rebuilds the festoons from the window surfaces (topmost first) and
    // advances the swing. Taskbars and screen bottoms get no lights; parts of
    // an edge hidden by other windows or too close to the screen top neither.
    void update(float dt, const std::vector<Surface>& surfaces, float wind);
    void clear();
    const std::vector<Festoon>& festoons() const { return festoons_; }

private:
    struct Swing {
        float left = 0.0f, top = 0.0f;  // last seen position
        float vx = 0.0f, vy = 0.0f;     // smoothed window velocity, px/s
        float angle = 0.0f, omega = 0.0f;
        bool seen = false;
    };
    std::unordered_map<std::uint64_t, Swing> swing_;
    std::vector<Festoon> festoons_;
};

}  // namespace sim
