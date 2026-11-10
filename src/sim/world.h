// Facade over the whole snow simulation. The platform layer feeds it monitors,
// windows (z-ordered) and the pointer, and reads back render data.
// Coordinates: physical px in the overlay's space, y grows downwards.
#pragma once

#include <cstdint>
#include <vector>

#include "sim/flakes.h"
#include "sim/rng.h"
#include "sim/snowballs.h"
#include "sim/surfaces.h"
#include "sim/types.h"

namespace sim {

struct SnowConfig {
    // Flakes spawned per second per 1000 logical (DPI-independent) px of width.
    float spawnRate = 12.0f;
    std::size_t maxFlakes = 6000;
    // Horizontal wind, logical px/s (positive = to the right).
    float wind = 0.0f;
    // 0..1: how much the wind varies over time.
    float gustiness = 0.35f;
};

struct PointerInput {
    bool valid = false;
    Vec2 pos;              // overlay space
    Vec2 vel;              // px/s, smoothed by the caller
    bool sculpt = false;   // sculpt modifier held (Ctrl+Alt)
    bool pressed = false;  // left button down
};

class World {
public:
    explicit World(std::uint64_t seed = 1);

    // --- inputs -------------------------------------------------------------
    // Replaces the display layout. Flakes off-screen are culled; screen-bottom
    // snow is reset.
    void setMonitors(std::vector<Monitor> monitors);
    // Top-level windows, topmost first. Piles of windows that moved, vanished
    // or got covered collapse into falling snow; resized ones are rescaled.
    void setWindows(const std::vector<WindowInfo>& windowsTopFirst);
    void setPointer(const PointerInput& p) { pointer_ = p; }
    void setConfig(const SnowConfig& cfg);

    // Fill the screens with flakes at steady-state density.
    void prewarm();
    void step(float dt);
    // The "shovel": removes all snow (falling, lying, snowballs).
    void clear();

    // --- outputs ------------------------------------------------------------
    const std::vector<Monitor>& monitors() const { return monitors_; }
    const RectF& bounds() const { return bounds_; }
    const SnowConfig& config() const { return cfg_; }
    const FlakePool& flakes() const { return flakes_; }
    const std::vector<Surface>& surfaces() const { return surfaces_; }
    const std::vector<Snowball>& snowballs() const { return balls_; }
    float time() const { return time_; }
    float currentWind() const { return windNow_; }
    bool holdingSnowball() const;

    // Total snow lying on surfaces, px^2 (for tests/diagnostics).
    float lyingArea() const;

private:
    FlakeSpawn makeFlake(float x, float y, float scale);
    void addDebris(float x, float y, Vec2 vel, float area, float scale);
    bool spawnRoom() const { return flakes_.size() < cfg_.maxFlakes; }
    void spawn(float dt);
    void cull();
    float floorAt(float x) const;
    float windMargin() const;
    float averageScale() const;
    float scaleAt(Vec2 p) const;
    float computeWind() const;

    Surface makeSurface(std::uint64_t id, const RectF& rect, bool ground, bool taskbar);
    void collapseCells(Surface& s, std::size_t i0, std::size_t i1, Vec2 vel);
    void collapseAll(Surface& s, Vec2 vel) { collapseCells(s, 0, s.pile.size(), vel); }
    void recomputeVisibility();
    void rebuildIndex();

    void landFlakes();
    void relaxPiles();
    void applyMitten();
    void updateSculpt();
    void growHeld(Snowball& b, float movedX);
    void stepBalls(float dt);
    // heldPointerY: for a dragged ball, the cursor y. Surfaces the cursor has
    // been pulled well below are passed through (lets you lift a ball off a ledge).
    void collideBallWithSurfaces(Snowball& b, float dt, float heldPointerY = kNoSurface);

    SnowConfig cfg_;
    std::vector<Monitor> monitors_;
    RectF bounds_;
    FlakePool flakes_;
    Rng rng_;
    float time_ = 0.0f;
    float windNow_ = 0.0f;

    // Window surfaces in z-order (topmost first), followed by screen bottoms.
    std::vector<Surface> surfaces_;
    SurfaceIndex index_;
    std::vector<Heightmap::Spill> spillScratch_;

    PointerInput pointer_;
    Vec2 prevPointer_;
    bool havePrevPointer_ = false;
    bool wasPressed_ = false;
    float mittenCarry_ = 0.0f;

    std::vector<Snowball> balls_;
};

}  // namespace sim
