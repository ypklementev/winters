// Screen layout and the mapping between macOS coordinates and simulation space.
//
// macOS has three coordinate systems in play:
//  - Cocoa (NSScreen, NSEvent): points, origin at the bottom-left of the main
//    display, y up;
//  - Quartz "global display" (CGWindowList): points, origin at the top-left of
//    the main display, y down;
//  - the simulation: y down, origin at the top-left of all screens together,
//    in units of 1/scale point.
// scale is the largest backing scale factor of all screens (2 on Retina), so
// on a Retina display one simulation unit is one physical pixel, exactly as
// on Windows. Monitor::scale is that same factor: sizes tuned in logical px
// on Windows come out in points here.
#pragma once

#import <AppKit/AppKit.h>

#include <cstdint>
#include <vector>

#include "sim/surfaces.h"
#include "sim/types.h"

struct ScreenDesc {
    NSScreen* screen = nil;
    CGRect frame{};    // global display coordinates (points, y down)
    CGRect visible{};  // frame minus the menu bar and the Dock
    double backing = 1.0;  // device pixels per point
};

class ScreenLayout {
public:
    static ScreenLayout query();

    const std::vector<ScreenDesc>& screens() const { return screens_; }
    bool empty() const { return screens_.empty(); }
    // Simulation units per point.
    double scale() const { return scale_; }
    // Union of all screens, global display coordinates.
    CGRect bounds() const { return bounds_; }

    sim::RectF toSim(CGRect global) const;
    sim::Vec2 toSim(CGPoint global) const;
    CGPoint toGlobal(sim::Vec2 p) const;

    std::vector<sim::Monitor> monitors() const;

    // The menu bars and Docks, as windows for the simulation (topmost first).
    //  - A menu bar hides the top edges of windows pressed against it, as a
    //    maximised window's top edge on Windows is too close to the screen top.
    //  - A Dock is a "taskbar": snow lies on its top edge, not on the screen
    //    bottom behind it. Its exact width needs the Accessibility permission,
    //    so it spans the whole screen, like the Windows taskbar.
    void appendSystemBars(std::vector<sim::WindowInfo>& out) const;

    // Cocoa (NSEvent/NSScreen) <-> global display coordinates.
    static CGPoint cocoaToGlobal(NSPoint p);
    static CGRect cocoaToGlobal(NSRect r);
    static NSRect globalToCocoa(CGRect r);

private:
    std::vector<ScreenDesc> screens_;
    double scale_ = 1.0;
    CGRect bounds_{};
};
