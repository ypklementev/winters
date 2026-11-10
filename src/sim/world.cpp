#include "sim/world.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sim {

namespace {

// --- falling flakes (logical px; multiplied by the monitor scale) ----------
constexpr float kMinRadius = 1.0f;
constexpr float kMaxRadius = 3.2f;
constexpr float kBaseFall = 32.0f;       // px/s
constexpr float kFallPerRadius = 20.0f;  // bigger flakes fall faster
constexpr float kFallJitter = 16.0f;
constexpr float kSlowestFall = kBaseFall + kFallPerRadius * kMinRadius;
constexpr float kCullSlack = 64.0f;
constexpr std::size_t kDebrisReserve = 5000;  // pool room for collapse/sweep debris

// --- lying snow ------------------------------------------------------------
constexpr float kCellWidth = 2.0f;     // px per height-map cell
constexpr float kDeposit = 0.25f;      // landed area = kDeposit * pi * r^2
constexpr float kMaxPile = 60.0f;      // max pile height
constexpr float kMaxSlope = 0.7f;      // tan(angle of repose) ~ 35 degrees
constexpr int kRelaxIterations = 2;
constexpr float kMinHeadroom = 6.0f;   // edges this close to the screen top hold no snow
constexpr float kSpillArea = 3.0f;     // spilled area per falling particle
constexpr float kClumpArea = 5.0f;     // collapse area per particle (minimum)
constexpr int kMaxCollapseParticles = 1200;
constexpr std::uint64_t kGroundIdBase = 0xF1A4E00000000000ull;

// --- mitten (cursor) ----------------------------------------------------------
constexpr float kMittenRadius = 20.0f;
constexpr float kMittenMinSpeed = 60.0f;  // px/s; slower movement doesn't sweep
constexpr float kMittenArea = 4.0f;
constexpr float kMittenMaxJump = 400.0f;  // longer moves are teleports, not sweeps

// --- snowballs ------------------------------------------------------------------
constexpr float kGravity = 1800.0f;
constexpr float kBallStartRadius = 4.0f;
constexpr float kBallMaxRadius = 90.0f;
constexpr float kBallGrow = 1.5f;  // area gained per area of snow rolled up
constexpr float kBallBite = 0.3f;  // snow depth picked up per px rolled
constexpr std::size_t kMaxBalls = 40;
constexpr float kBallMaxThrow = 2500.0f;

float radiusForArea(float area, float scale) {
    const float r = std::sqrt(area / (kDeposit * kPi));
    return std::clamp(r, 0.8f * scale, 3.5f * scale);
}

}  // namespace

World::World(std::uint64_t seed) : flakes_(cfg_.maxFlakes + kDebrisReserve), rng_(seed) {}

// ============================================================================
// Configuration & layout
// ============================================================================

void World::setConfig(const SnowConfig& cfg) {
    cfg_ = cfg;
    const std::size_t cap = cfg_.maxFlakes + kDebrisReserve;
    if (flakes_.capacity() != cap) flakes_.setCapacity(cap);
}

void World::setMonitors(std::vector<Monitor> monitors) {
    monitors_ = std::move(monitors);
    bounds_ = {};
    for (const auto& m : monitors_) bounds_ = unite(bounds_, m.bounds);

    // Screen bottoms: drop the old ones, add one per monitor.
    surfaces_.erase(std::remove_if(surfaces_.begin(), surfaces_.end(),
                                   [](const Surface& s) { return s.ground; }),
                    surfaces_.end());
    for (std::size_t i = 0; i < monitors_.size(); ++i) {
        const RectF& b = monitors_[i].bounds;
        surfaces_.push_back(
            makeSurface(kGroundIdBase + i, {b.left, b.bottom, b.right, b.bottom + 1.0f}, true, false));
    }
    recomputeVisibility();
    rebuildIndex();
    cull();
}

Surface World::makeSurface(std::uint64_t id, const RectF& rect, bool ground, bool taskbar) {
    Surface s;
    s.id = id;
    s.rect = rect;
    s.ground = ground;
    s.taskbar = taskbar;
    s.scale = scaleAt({(rect.left + rect.right) * 0.5f, rect.top - 1.0f});
    const float cw = kCellWidth * s.scale;
    const auto cells = static_cast<std::size_t>(std::max(1.0f, std::ceil(rect.width() / cw)));
    s.pile = Heightmap(cells, cw);
    return s;
}

void World::setWindows(const std::vector<WindowInfo>& windows) {
    std::unordered_map<std::uint64_t, std::size_t> old;
    old.reserve(surfaces_.size());
    for (std::size_t i = 0; i < surfaces_.size(); ++i) {
        if (!surfaces_[i].ground) old.emplace(surfaces_[i].id, i);
    }
    std::vector<bool> used(surfaces_.size(), false);

    std::vector<Surface> next;
    next.reserve(windows.size() + monitors_.size());
    for (const WindowInfo& w : windows) {
        if (w.rect.empty()) continue;
        auto it = old.find(w.id);
        if (it == old.end() || used[it->second]) {
            next.push_back(makeSurface(w.id, w.rect, false, w.taskbar));
            continue;
        }
        used[it->second] = true;
        Surface s = std::move(surfaces_[it->second]);
        const bool topChanged = std::abs(s.rect.top - w.rect.top) > 0.5f;
        const bool leftChanged = std::abs(s.rect.left - w.rect.left) > 0.5f;
        const bool widthChanged = std::abs(s.rect.width() - w.rect.width()) > 0.5f;
        if (topChanged || (leftChanged && !widthChanged)) {
            // Moved: the snow falls off where it was.
            collapseAll(s, {});
            Surface fresh = makeSurface(w.id, w.rect, false, w.taskbar);
            fresh.spillCarry = 0.0f;
            s = std::move(fresh);
        } else if (widthChanged) {
            const float cw = s.pile.cellWidth();
            s.pile.resample(static_cast<std::size_t>(std::max(1.0f, std::ceil(w.rect.width() / cw))));
            s.rect = w.rect;
            s.settled = false;
        } else {
            s.rect = w.rect;  // sub-pixel / bottom-only change
        }
        s.taskbar = w.taskbar;
        next.push_back(std::move(s));
    }

    // Closed / minimized / hidden windows: everything falls.
    for (std::size_t i = 0; i < surfaces_.size(); ++i) {
        if (surfaces_[i].ground || used[i]) continue;
        collapseAll(surfaces_[i], {});
    }
    for (Surface& s : surfaces_) {
        if (s.ground) next.push_back(std::move(s));
    }
    surfaces_ = std::move(next);
    recomputeVisibility();
    rebuildIndex();
}

void World::recomputeVisibility() {
    std::vector<RectF> windowOccluders;  // windows above the current one
    std::vector<RectF> groundOccluders;  // taskbars + monitors continuing below
    for (const Surface& s : surfaces_) {
        if (s.taskbar) groundOccluders.push_back(s.rect);
    }

    for (std::size_t k = 0; k < surfaces_.size(); ++k) {
        Surface& s = surfaces_[k];
        std::vector<Span> spans;
        if (s.ground) {
            std::vector<RectF> occ = groundOccluders;
            // A screen bottom that continues into a monitor below isn't a floor.
            for (const Monitor& m : monitors_) {
                if (m.bounds.top >= s.edgeY() - 0.5f) occ.push_back(m.bounds);
            }
            spans = visibleEdgeSpans(s.rect.left, s.rect.right, s.edgeY(), occ, monitors_, 0.0f);
        } else {
            spans = visibleEdgeSpans(s.rect.left, s.rect.right, s.edgeY(), windowOccluders, monitors_,
                                     kMinHeadroom * s.scale);
            windowOccluders.push_back(s.rect);
        }

        // Mark cells; snow on cells that just became hidden falls down.
        std::size_t span = 0;
        std::size_t runStart = 0;
        bool inRun = false;
        for (std::size_t i = 0; i < s.pile.size(); ++i) {
            const float cx = s.cellCenterX(i);
            while (span < spans.size() && spans[span].right <= cx) ++span;
            const bool vis = span < spans.size() && spans[span].left <= cx;
            const bool hide = !vis && s.pile.height(i) > 0.0f;
            if (hide && !inRun) {
                runStart = i;
                inRun = true;
            } else if (!hide && inRun) {
                collapseCells(s, runStart, i, {});
                inRun = false;
            }
            s.pile.setVisible(i, vis);
        }
        if (inRun) collapseCells(s, runStart, s.pile.size(), {});
        s.settled = false;
    }
}

void World::rebuildIndex() { index_.build(surfaces_, bounds_); }

// ============================================================================
// Helpers
// ============================================================================

float World::averageScale() const {
    if (monitors_.empty()) return 1.0f;
    float s = 0.0f;
    for (const auto& m : monitors_) s += m.scale;
    return s / static_cast<float>(monitors_.size());
}

float World::scaleAt(Vec2 p) const {
    for (const auto& m : monitors_) {
        if (m.bounds.contains(p)) return m.scale;
    }
    // Off-screen: use the horizontally nearest monitor.
    float best = 1e30f;
    float scale = averageScale();
    for (const auto& m : monitors_) {
        const float d = p.x < m.bounds.left ? m.bounds.left - p.x
                        : p.x >= m.bounds.right ? p.x - m.bounds.right
                                                : 0.0f;
        if (d < best) {
            best = d;
            scale = m.scale;
        }
    }
    return scale;
}

float World::computeWind() const {
    if (cfg_.wind == 0.0f) return 0.0f;
    const float g = cfg_.gustiness;
    const float t = time_;
    const float gust = 0.55f * std::sin(0.31f * t) + 0.3f * std::sin(0.97f * t + 1.3f) +
                       0.15f * std::sin(2.3f * t + 0.4f);
    return cfg_.wind * std::max(0.1f, 1.0f + g * gust);
}

float World::windMargin() const {
    if (cfg_.wind == 0.0f || bounds_.empty()) return 0.0f;
    const float maxWind = std::abs(cfg_.wind) * (1.0f + cfg_.gustiness);
    const float travel = maxWind * bounds_.height() / kSlowestFall;
    return std::min(travel, bounds_.width());
}

float World::lyingArea() const {
    float a = 0.0f;
    for (const Surface& s : surfaces_) a += s.pile.totalArea();
    return a;
}

bool World::holdingSnowball() const {
    return std::any_of(balls_.begin(), balls_.end(), [](const Snowball& b) { return b.held; });
}

FlakeSpawn World::makeFlake(float x, float y, float scale) {
    FlakeSpawn f;
    const float t = rng_.uniform();
    const float r = kMinRadius + (kMaxRadius - kMinRadius) * t * t;  // mostly small
    f.radius = r * scale;
    f.fallSpeed = (kBaseFall + kFallPerRadius * r + rng_.uniform(0.0f, kFallJitter)) * scale;
    f.swayAmp = rng_.uniform(4.0f, 16.0f) * scale;
    f.swayFreq = rng_.uniform(0.6f, 1.8f);
    f.phase = rng_.uniform(0.0f, 2.0f * kPi);
    f.alpha = 0.55f + 0.45f * (r - kMinRadius) / (kMaxRadius - kMinRadius);
    f.scale = scale;
    f.pos = {x, y};
    f.vel = {windNow_ * scale, f.fallSpeed};
    return f;
}

void World::addDebris(float x, float y, Vec2 vel, float area, float scale) {
    FlakeSpawn f;
    f.pos = {x, y};
    f.vel = vel;
    f.radius = radiusForArea(area, scale);
    f.fallSpeed = rng_.uniform(70.0f, 120.0f) * scale;
    f.swayAmp = rng_.uniform(2.0f, 7.0f) * scale;
    f.swayFreq = rng_.uniform(0.8f, 2.0f);
    f.phase = rng_.uniform(0.0f, 2.0f * kPi);
    f.alpha = 0.95f;
    f.scale = scale;
    flakes_.add(f);
}

// ============================================================================
// Falling snow
// ============================================================================

void World::spawn(float dt) {
    // floor(expected + U) gives the right mean without per-monitor state.
    auto count = [&](float logicalWidth) {
        const float expected = cfg_.spawnRate * logicalWidth / 1000.0f * dt;
        return static_cast<int>(expected + rng_.uniform());
    };

    for (std::size_t mi = 0; mi < monitors_.size(); ++mi) {
        const Monitor& m = monitors_[mi];
        const int n = count(m.bounds.width() / m.scale);
        for (int i = 0; i < n && spawnRoom(); ++i) {
            const float x = rng_.uniform(m.bounds.left, m.bounds.right);
            // A monitor directly above already feeds this column.
            bool covered = false;
            for (std::size_t mj = 0; mj < monitors_.size(); ++mj) {
                if (mj != mi && monitors_[mj].bounds.contains({x, m.bounds.top - 1.0f})) {
                    covered = true;
                    break;
                }
            }
            if (covered) continue;
            FlakeSpawn f = makeFlake(x, 0.0f, m.scale);
            f.pos.y = m.bounds.top - f.radius;
            flakes_.add(f);
        }
    }

    // Upwind band outside the virtual screen, so the upwind edge isn't bare.
    const float margin = windMargin();
    if (margin > 0.0f) {
        const float s = averageScale();
        const int n = count(margin / s);
        for (int i = 0; i < n && spawnRoom(); ++i) {
            const float x = cfg_.wind > 0.0f ? rng_.uniform(bounds_.left - margin, bounds_.left)
                                             : rng_.uniform(bounds_.right, bounds_.right + margin);
            FlakeSpawn f = makeFlake(x, 0.0f, s);
            f.pos.y = bounds_.top - f.radius;
            flakes_.add(f);
        }
    }
}

void World::prewarm() {
    for (const auto& m : monitors_) {
        const float logicalWidth = m.bounds.width() / m.scale;
        const float avgFall = (kBaseFall + kFallPerRadius * 1.6f + kFallJitter * 0.5f) * m.scale;
        const float crossTime = m.bounds.height() / avgFall;
        const int n = static_cast<int>(cfg_.spawnRate * logicalWidth / 1000.0f * crossTime);
        for (int i = 0; i < n && spawnRoom(); ++i) {
            flakes_.add(makeFlake(rng_.uniform(m.bounds.left, m.bounds.right),
                                  rng_.uniform(m.bounds.top, m.bounds.bottom), m.scale));
        }
    }
}

float World::floorAt(float x) const {
    float floor = -1e30f;
    for (const auto& m : monitors_) {
        if (m.bounds.containsX(x)) floor = std::max(floor, m.bounds.bottom);
    }
    return floor > -1e30f ? floor : bounds_.bottom;
}

void World::cull() {
    const float margin = windMargin() + kCullSlack;
    const float minX = bounds_.left - margin;
    const float maxX = bounds_.right + margin;
    for (std::size_t i = 0; i < flakes_.size();) {
        const float x = flakes_.x[i];
        const float top = flakes_.y[i] - flakes_.radius[i];
        if (x < minX || x > maxX || top > floorAt(x)) {
            flakes_.remove(i);
        } else {
            ++i;
        }
    }
}

void World::landFlakes() {
    for (std::size_t i = 0; i < flakes_.size();) {
        const float x = flakes_.x[i];
        const float r = flakes_.radius[i];
        const float y0 = flakes_.py[i] + r * 0.5f;
        const float y1 = flakes_.y[i] + r * 0.5f;
        int best = -1;
        float bestLine = kNoSurface;
        if (y1 > y0) {
            for (std::uint32_t si : index_.at(x)) {
                const float line = surfaces_[si].lineAt(x);
                if (line != kNoSurface && y0 <= line && y1 >= line && line < bestLine) {
                    bestLine = line;
                    best = static_cast<int>(si);
                }
            }
        }
        if (best < 0) {
            ++i;
            continue;
        }
        Surface& s = surfaces_[static_cast<std::size_t>(best)];
        s.pile.deposit(x - s.rect.left, r, kDeposit * kPi * r * r, kMaxPile * s.scale);
        s.settled = false;
        flakes_.remove(i);
    }
}

// ============================================================================
// Lying snow
// ============================================================================

void World::collapseCells(Surface& s, std::size_t i0, std::size_t i1, Vec2 vel) {
    const float cw = s.pile.cellWidth();
    float total = 0.0f;
    for (std::size_t i = i0; i < i1; ++i) total += s.pile.height(i) * cw;
    if (total <= 0.0f) return;

    const float s2 = s.scale * s.scale;
    const std::size_t room = flakes_.capacity() - flakes_.size();
    const float budget = static_cast<float>(std::min<std::size_t>(room, kMaxCollapseParticles));
    const float per = budget >= 1.0f ? std::max(kClumpArea * s2, total / budget) : total + 1.0f;

    float carry = 0.0f;
    for (std::size_t i = i0; i < i1; ++i) {
        const float h = s.pile.height(i);
        if (h <= 0.0f) continue;
        carry += h * cw;
        while (carry >= per) {
            const float x = s.cellCenterX(i) + rng_.uniform(-0.5f, 0.5f) * cw;
            const float y = s.edgeY() - rng_.uniform() * h;
            const Vec2 v = vel + Vec2{rng_.uniform(-25.0f, 25.0f), rng_.uniform(-30.0f, 10.0f)} * s.scale;
            addDebris(x, y, v, per, s.scale);
            carry -= per;
        }
        s.pile.setHeight(i, 0.0f);
    }
    s.settled = false;
}

void World::relaxPiles() {
    for (Surface& s : surfaces_) {
        if (s.settled) continue;
        spillScratch_.clear();
        s.settled = !s.pile.relax(kMaxSlope, kRelaxIterations, spillScratch_);
        const float unit = kSpillArea * s.scale * s.scale;
        const float width = s.pile.cellWidth() * static_cast<float>(s.pile.size());
        for (const auto& sp : spillScratch_) {
            s.spillCarry += sp.area;
            while (s.spillCarry >= unit) {
                const float dir = sp.localX < width * 0.5f ? -1.0f : 1.0f;
                const Vec2 v{dir * rng_.uniform(5.0f, 25.0f) * s.scale, rng_.uniform(0.0f, 20.0f) * s.scale};
                addDebris(s.rect.left + sp.localX, s.edgeY() - 1.0f, v, unit, s.scale);
                s.spillCarry -= unit;
            }
        }
    }
}

// ============================================================================
// Mitten (cursor sweeping)
// ============================================================================

void World::applyMitten() {
    if (!pointer_.valid || pointer_.sculpt) return;
    const Vec2 b = pointer_.pos;
    Vec2 a = havePrevPointer_ ? prevPointer_ : b;
    const float scale = scaleAt(b);
    const float speed = length(pointer_.vel);
    if (speed < kMittenMinSpeed * scale) return;
    if (length(b - a) > kMittenMaxJump * scale) a = b;

    const float R = kMittenRadius * scale;
    const Vec2 seg = b - a;
    const float segLen2 = dot(seg, seg);
    auto distToSeg = [&](Vec2 p) {
        const float t = segLen2 > 0.0f ? std::clamp(dot(p - a, seg) / segLen2, 0.0f, 1.0f) : 0.0f;
        return length(p - (a + seg * t));
    };

    // Flakes in the air get swept along.
    for (std::size_t i = 0; i < flakes_.size(); ++i) {
        const Vec2 p{flakes_.x[i], flakes_.y[i]};
        const float d = distToSeg(p);
        if (d > R + flakes_.radius[i]) continue;
        flakes_.vx[i] = flakes_.vx[i] * 0.3f + pointer_.vel.x * 0.7f + rng_.uniform(-30.0f, 30.0f) * scale;
        flakes_.vy[i] = flakes_.vy[i] * 0.3f + pointer_.vel.y * 0.7f - rng_.uniform(0.0f, 60.0f) * scale;
    }

    // Lying snow inside the swept capsule is knocked off.
    const float x0 = std::min(a.x, b.x) - R;
    const float x1 = std::max(a.x, b.x) + R;
    const int samples = std::clamp(static_cast<int>(std::sqrt(segLen2) / (R * 0.5f)) + 1, 1, 24);
    index_.forRange(x0, x1, [&](std::uint32_t si) {
        Surface& s = surfaces_[si];
        const float cw = s.pile.cellWidth();
        bool touched = false;
        for (int k = 0; k < samples; ++k) {
            const float t = samples == 1 ? 1.0f : static_cast<float>(k) / static_cast<float>(samples - 1);
            const Vec2 c = a + seg * t;
            if (c.y - R > s.edgeY() || c.y + R < s.edgeY() - kMaxPile * s.scale) continue;
            const int i0 = std::max(0, static_cast<int>((c.x - R - s.rect.left) / cw));
            const int i1 = std::min(static_cast<int>(s.pile.size()) - 1,
                                    static_cast<int>((c.x + R - s.rect.left) / cw));
            for (int i = i0; i <= i1; ++i) {
                const auto ui = static_cast<std::size_t>(i);
                const float h = s.pile.height(ui);
                if (h <= 0.0f || !s.pile.visible(ui)) continue;
                const float dx = s.cellCenterX(ui) - c.x;
                if (std::abs(dx) >= R) continue;
                const float half = std::sqrt(R * R - dx * dx);
                const float top = s.edgeY() - h;
                if (top >= c.y + half || s.edgeY() <= c.y - half) continue;
                const float removed = s.pile.lowerTo(ui, s.edgeY() - (c.y + half));
                if (removed <= 0.0f) continue;
                touched = true;
                mittenCarry_ += removed;
                const float unit = kMittenArea * s.scale * s.scale;
                while (mittenCarry_ >= unit) {
                    const Vec2 v = pointer_.vel * 0.55f +
                                   Vec2{rng_.uniform(-40.0f, 40.0f), -rng_.uniform(60.0f, 180.0f)} * s.scale;
                    addDebris(s.cellCenterX(ui), top + rng_.uniform() * h - 1.0f, v, unit, s.scale);
                    mittenCarry_ -= unit;
                }
            }
        }
        if (touched) s.settled = false;
    });

    // Loose snowballs get kicked.
    for (Snowball& ball : balls_) {
        if (ball.held) continue;
        if (distToSeg(ball.pos) > R + ball.radius) continue;
        const float k = std::clamp(12.0f * scale / ball.radius, 0.05f, 0.6f);
        ball.vel = ball.vel + (pointer_.vel - ball.vel) * k;
        ball.stillTime = 0.0f;
    }
}

// ============================================================================
// Snowballs
// ============================================================================

void World::updateSculpt() {
    const bool pressed = pointer_.valid && pointer_.sculpt && pointer_.pressed;
    auto heldIt = std::find_if(balls_.begin(), balls_.end(), [](const Snowball& b) { return b.held; });

    if (pressed && !wasPressed_ && heldIt == balls_.end()) {
        const Vec2 p = pointer_.pos;
        const float scale = scaleAt(p);
        // 1) Grab an existing ball.
        for (Snowball& b : balls_) {
            if (length(b.pos - p) < b.radius + 6.0f * scale) {
                b.held = true;
                break;
            }
        }
        heldIt = std::find_if(balls_.begin(), balls_.end(), [](const Snowball& b) { return b.held; });
        // 2) Or start a new one from snow under the cursor.
        if (heldIt == balls_.end()) {
            bool onSnow = false;
            for (std::uint32_t si : index_.at(p.x)) {
                const Surface& s = surfaces_[si];
                const float line = s.lineAt(p.x);
                if (line == kNoSurface) continue;
                const float h = s.edgeY() - line;
                if (h >= 1.0f * scale && p.y >= line - 14.0f * scale && p.y <= s.edgeY() + 6.0f * scale) {
                    onSnow = true;
                    break;
                }
            }
            if (onSnow) {
                if (balls_.size() >= kMaxBalls) balls_.erase(balls_.begin());
                Snowball b;
                b.radius = kBallStartRadius * scale;
                b.scale = scale;
                b.pos = b.prevPos = p;
                b.held = true;
                balls_.push_back(b);
                heldIt = balls_.end() - 1;
            }
        }
    }

    if (heldIt != balls_.end()) {
        Snowball& b = *heldIt;
        if (!pressed) {
            b.held = false;
            b.vel = pointer_.vel;
            const float sp = length(b.vel);
            const float maxSp = kBallMaxThrow * b.scale;
            if (sp > maxSp) b.vel = b.vel * (maxSp / sp);
            b.stillTime = 0.0f;
        } else {
            b.prevPos = b.pos;
            b.pos = pointer_.pos;
            b.vel = pointer_.vel;
            collideBallWithSurfaces(b, kFixedDt, pointer_.pos.y);
            growHeld(b, std::abs(b.pos.x - b.prevPos.x));
        }
    }
    wasPressed_ = pressed;
}

void World::growHeld(Snowball& b, float movedX) {
    if (movedX <= 0.0f) return;
    float removed = 0.0f;
    const float r = b.radius;
    const float bite = kBallBite * movedX;
    index_.forRange(b.pos.x - r, b.pos.x + r, [&](std::uint32_t si) {
        Surface& s = surfaces_[si];
        const float edge = s.edgeY();
        if (edge < b.pos.y - r || edge - kMaxPile * s.scale > b.pos.y + r + 4.0f * s.scale) return;
        const float cw = s.pile.cellWidth();
        const int i0 = std::max(0, static_cast<int>((b.pos.x - r - s.rect.left) / cw));
        const int i1 = std::min(static_cast<int>(s.pile.size()) - 1, static_cast<int>((b.pos.x + r - s.rect.left) / cw));
        for (int i = i0; i <= i1; ++i) {
            const auto ui = static_cast<std::size_t>(i);
            const float h = s.pile.height(ui);
            if (h <= 0.0f || !s.pile.visible(ui)) continue;
            const float dx = s.cellCenterX(ui) - b.pos.x;
            if (std::abs(dx) >= r * 0.6f) continue;
            const float bottom = b.pos.y + std::sqrt(r * r - dx * dx);
            if (bottom < edge - h - 2.0f * s.scale) continue;  // not touching the snow
            removed += s.pile.lowerTo(ui, h - bite);
            s.settled = false;
        }
    });
    if (removed > 0.0f) {
        const float area = kPi * b.radius * b.radius + removed * kBallGrow;
        b.radius = std::min(std::sqrt(area / kPi), kBallMaxRadius * b.scale);
    }
}

void World::collideBallWithSurfaces(Snowball& b, float dt, float heldPointerY) {
    const float r = b.radius;
    float bestReq = kNoSurface;
    float bestDx = 0.0f;
    for (float f : {-0.7f, -0.35f, 0.0f, 0.35f, 0.7f}) {
        const float dx = f * r;
        const float x = b.pos.x + dx;
        const float c = std::sqrt(r * r - dx * dx);
        const float prevBottom = b.prevPos.y + c;
        for (std::uint32_t si : index_.at(x)) {
            const float line = surfaces_[si].lineAt(x);
            if (line == kNoSurface) continue;
            // Surfaces that were above the ball are in front of it: pass through.
            if (line < prevBottom - 4.0f * b.scale) continue;
            if (heldPointerY != kNoSurface && heldPointerY > line + 0.5f * r) continue;
            const float req = line - c;
            if (req < bestReq) {
                bestReq = req;
                bestDx = dx;
            }
        }
    }
    if (bestReq == kNoSurface || b.pos.y <= bestReq) return;
    b.pos.y = bestReq;
    b.contact = true;
    if (heldPointerY != kNoSurface) return;  // dragged: just ride on top
    if (b.vel.y > 0.0f) b.vel.y = b.vel.y > 150.0f * b.scale ? -b.vel.y * 0.15f : 0.0f;
    b.vel.x *= std::exp(-4.0f * dt);
    // Resting on a corner only: roll off towards the unsupported side.
    if (std::abs(bestDx) > 0.3f * r) b.vel.x -= (bestDx > 0.0f ? 1.0f : -1.0f) * kGravity * b.scale * 0.35f * dt;
}

void World::stepBalls(float dt) {
    if (balls_.empty()) return;
    for (Snowball& b : balls_) {
        b.contact = false;
        if (b.held) continue;
        b.prevPos = b.pos;
        b.vel.y += kGravity * b.scale * dt;
        b.vel = b.vel * std::exp(-0.3f * dt);
        b.pos = b.pos + b.vel * dt;
    }
    for (int round = 0; round < 2; ++round) {
        for (Snowball& b : balls_) {
            if (!b.held) collideBallWithSurfaces(b, dt);
        }
        resolveBallContacts(balls_, 3);
    }
    for (std::size_t i = 0; i < balls_.size();) {
        Snowball& b = balls_[i];
        if (!b.held) {
            if (b.pos.x - b.radius < bounds_.left) {
                b.pos.x = bounds_.left + b.radius;
                b.vel.x = std::abs(b.vel.x) * 0.3f;
            } else if (b.pos.x + b.radius > bounds_.right) {
                b.pos.x = bounds_.right - b.radius;
                b.vel.x = -std::abs(b.vel.x) * 0.3f;
            }
            if (b.contact && length(b.vel) < 8.0f * b.scale) {
                b.stillTime += dt;
                if (b.stillTime > 0.3f) b.vel = {};
            } else {
                b.stillTime = 0.0f;
            }
            if (b.pos.y - b.radius > bounds_.bottom) {
                balls_.erase(balls_.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
        }
        ++i;
    }
}

// ============================================================================
// Step / clear
// ============================================================================

void World::step(float dt) {
    time_ += dt;
    if (monitors_.empty()) return;
    windNow_ = computeWind();
    spawn(dt);
    applyMitten();
    updateSculpt();
    integrateFlakes(flakes_, dt, windNow_, time_);
    landFlakes();
    stepBalls(dt);
    relaxPiles();
    cull();
    prevPointer_ = pointer_.pos;
    havePrevPointer_ = pointer_.valid;
}

void World::clear() {
    flakes_.clear();
    for (Surface& s : surfaces_) {
        s.pile.clear();
        s.spillCarry = 0.0f;
        s.settled = true;
    }
    balls_.clear();
    mittenCarry_ = 0.0f;
}

}  // namespace sim
