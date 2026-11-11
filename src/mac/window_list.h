// Visible windows of other apps, topmost first (CGWindowListCopyWindowInfo).
// macOS has no public event for "some window moved", so the list is polled;
// one query costs ~0.2 ms. Only window bounds are read, which needs no
// Screen Recording permission (titles would).
#pragma once

#import <CoreGraphics/CoreGraphics.h>

#include <cstdint>
#include <vector>

struct MacWindow {
    std::uint32_t id = 0;
    CGRect frame{};  // global display coordinates (points, y down)

    bool operator==(const MacWindow& o) const { return id == o.id && CGRectEqualToRect(frame, o.frame); }
};

// Windows on the current Space(s), in z-order, without our own, menus, the
// Dock and other system chrome.
std::vector<MacWindow> QueryWindows();

// Corner radius of standard windows, points. Not queryable through public
// API; it grew in macOS 26 (Tahoe), where windows with toolbars are rounder
// still: those lose a little snow at the ends, which looks fine.
double WindowCornerRadius();
