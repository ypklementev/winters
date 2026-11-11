#include "mac/screens.h"

#include <algorithm>

namespace {

constexpr std::uint64_t kMenuBarIdBase = 0x3E2B000000000000ull;
// Windows whose top edge is this close below the menu bar hold no snow.
constexpr double kMenuBarMargin = 2.0;

// Height of the main display (the one whose Cocoa origin is (0, 0)): flips y.
double MainHeight() {
    NSArray<NSScreen*>* screens = NSScreen.screens;
    return screens.count ? screens[0].frame.size.height : 0.0;
}

}  // namespace

CGPoint ScreenLayout::cocoaToGlobal(NSPoint p) { return CGPointMake(p.x, MainHeight() - p.y); }

CGRect ScreenLayout::cocoaToGlobal(NSRect r) {
    return CGRectMake(r.origin.x, MainHeight() - (r.origin.y + r.size.height), r.size.width, r.size.height);
}

NSRect ScreenLayout::globalToCocoa(CGRect r) {
    return NSMakeRect(r.origin.x, MainHeight() - (r.origin.y + r.size.height), r.size.width, r.size.height);
}

ScreenLayout ScreenLayout::query() {
    ScreenLayout l;
    bool first = true;
    for (NSScreen* s in NSScreen.screens) {
        ScreenDesc d;
        d.screen = s;
        d.frame = cocoaToGlobal(s.frame);
        d.visible = cocoaToGlobal(s.visibleFrame);
        d.backing = s.backingScaleFactor;
        l.scale_ = std::max(l.scale_, d.backing);
        l.bounds_ = first ? d.frame : CGRectUnion(l.bounds_, d.frame);
        first = false;
        l.screens_.push_back(d);
    }
    return l;
}

sim::Vec2 ScreenLayout::toSim(CGPoint p) const {
    return {static_cast<float>((p.x - bounds_.origin.x) * scale_), static_cast<float>((p.y - bounds_.origin.y) * scale_)};
}

sim::RectF ScreenLayout::toSim(CGRect r) const {
    const sim::Vec2 a = toSim(r.origin);
    const sim::Vec2 b = toSim(CGPointMake(CGRectGetMaxX(r), CGRectGetMaxY(r)));
    return {a.x, a.y, b.x, b.y};
}

CGPoint ScreenLayout::toGlobal(sim::Vec2 p) const {
    return CGPointMake(p.x / scale_ + bounds_.origin.x, p.y / scale_ + bounds_.origin.y);
}

std::vector<sim::Monitor> ScreenLayout::monitors() const {
    std::vector<sim::Monitor> out;
    for (const ScreenDesc& d : screens_) {
        sim::Monitor m;
        m.bounds = toSim(d.frame);
        m.work = toSim(d.visible);
        m.scale = static_cast<float>(scale_);
        out.push_back(m);
    }
    return out;
}

void ScreenLayout::appendMenuBars(std::vector<sim::WindowInfo>& out) const {
    std::uint64_t i = 0;
    for (const ScreenDesc& d : screens_) {
        const CGRect f = d.frame;
        const double top = CGRectGetMinY(d.visible) - CGRectGetMinY(f);
        if (top > 0.5) {
            sim::WindowInfo w;
            w.id = kMenuBarIdBase + i;
            w.rect = toSim(CGRectMake(f.origin.x, f.origin.y, f.size.width, top + kMenuBarMargin));
            out.push_back(w);
        }
        ++i;
    }
}
