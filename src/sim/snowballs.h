// Snowballs: rigid circles with gravity and collisions. A ball resting on top
// of another sticks to it (becomes its child and moves rigidly with it), so
// snowmen hold together and can be carried by their base.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sim/types.h"

namespace sim {

struct Snowball {
    std::uint32_t id = 0;
    std::uint32_t parent = 0;  // id of the ball this one is stuck on (0 = free)
    Vec2 offset;               // position relative to the parent while stuck
    Vec2 pos;
    Vec2 vel;
    Vec2 prevPos;
    float radius = 5.0f;
    float angle = 0.0f;        // rolling rotation, for the renderer
    float scale = 1.0f;
    bool held = false;         // being dragged by the cursor
    bool contact = false;      // touched ground or another ball this step
    float stillTime = 0.0f;
    float pushedAt = -1.0f;    // world time the cursor last pushed it
    float impactSpeed = 0.0f;  // hardest hit this step (px/s), for shattering
    float settleTime = 0.0f;   // how long it has rested on a candidate parent
    std::uint32_t settleOn = 0;
    float decoratedAt = -1.0f; // world time it became a snowman's head (-1 = not)

    // Moved by someone else this step (cursor or parent): infinite mass.
    bool kinematic() const { return held || parent != 0; }
};

// Ball-ball collision resolution. Bonded pairs (parent/child) are skipped;
// kinematic balls push others but are not pushed. Records impact speeds.
// Near-vertical contacts are sticky (no sliding, purely vertical separation).
void resolveBallContacts(std::vector<Snowball>& balls, int iterations);

// Index of the ball with this id, or -1.
int findBall(const std::vector<Snowball>& balls, std::uint32_t id);

// A complete snowman: three stuck balls, each clearly smaller than the one
// below, with nothing on top of the head.
struct Snowman {
    std::size_t base, middle, head;
};
std::vector<Snowman> findSnowmen(const std::vector<Snowball>& balls);

}  // namespace sim
