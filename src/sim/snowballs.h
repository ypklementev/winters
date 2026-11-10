// Snowballs: simple rigid circles with gravity, stacking and "sticky" snow
// contacts so they can be piled into a snowman.
#pragma once

#include <vector>

#include "sim/types.h"

namespace sim {

struct Snowball {
    Vec2 pos;
    Vec2 vel;
    Vec2 prevPos;
    float radius = 5.0f;
    float angle = 0.0f;    // rolling rotation, for the renderer
    float scale = 1.0f;
    bool held = false;     // being dragged by the cursor (infinite mass)
    bool contact = false;  // touched ground or another ball this step
    float stillTime = 0.0f;
    float pushedAt = -1.0f;  // world time the cursor last pushed it
};

// Ball-ball collision resolution. Near-vertical contacts are treated as
// sticky (no sliding, purely vertical separation) so stacks are stable.
void resolveBallContacts(std::vector<Snowball>& balls, int iterations);

}  // namespace sim
