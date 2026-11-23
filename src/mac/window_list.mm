#include "mac/window_list.h"

#import <AppKit/AppKit.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

namespace {

// Normal windows (0), floating panels (3) and modal panels (8). Higher levels
// are the Dock, menus, the menu bar, tooltips, screen savers and our overlay.
bool IsLedgeLayer(int layer) { return layer >= 0 && layer <= 8; }

}  // namespace

double WindowCornerRadius() {
    return NSProcessInfo.processInfo.operatingSystemVersion.majorVersion >= 26 ? 16.0 : 10.0;
}

std::vector<MacWindow> QueryWindows() {
    std::vector<MacWindow> out;
    CFArrayRef raw = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                kCGNullWindowID);
    if (!raw) return out;
    NSArray* list = CFBridgingRelease(raw);
    const int selfPid = getpid();
    out.reserve(list.count);
    for (NSDictionary* w in list) {
        if (!IsLedgeLayer([w[(id)kCGWindowLayer] intValue])) continue;
        if ([w[(id)kCGWindowOwnerPID] intValue] == selfPid) continue;
        NSNumber* alpha = w[(id)kCGWindowAlpha];
        if (alpha && alpha.doubleValue < 0.05) continue;  // invisible helper windows
        CGRect r;
        if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)w[(id)kCGWindowBounds], &r)) continue;
        if (r.size.width < 40 || r.size.height < 16) continue;
        out.push_back({static_cast<std::uint32_t>([w[(id)kCGWindowNumber] unsignedIntValue]), r});
    }
    return out;
}

namespace {

// Area of the union of rects (coordinate compression; a handful of rects).
double UnionArea(const std::vector<CGRect>& rects) {
    std::vector<double> xs;
    for (const CGRect& r : rects) {
        xs.push_back(CGRectGetMinX(r));
        xs.push_back(CGRectGetMaxX(r));
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    double area = 0.0;
    for (size_t i = 0; i + 1 < xs.size(); ++i) {
        const double x0 = xs[i], x1 = xs[i + 1];
        std::vector<std::pair<double, double>> spans;
        for (const CGRect& r : rects) {
            if (CGRectGetMinX(r) <= x0 && CGRectGetMaxX(r) >= x1) spans.emplace_back(CGRectGetMinY(r), CGRectGetMaxY(r));
        }
        std::sort(spans.begin(), spans.end());
        double covered = 0.0, top = -1e300, bottom = -1e300;
        for (const auto& [y0, y1] : spans) {
            if (y0 > bottom) {
                covered += bottom - top;
                top = y0;
                bottom = y1;
            } else {
                bottom = std::max(bottom, y1);
            }
        }
        covered += bottom - top;
        area += covered * (x1 - x0);
    }
    return area;
}

}  // namespace

bool IsFullscreenAppFrontmost() {
    NSRunningApplication* front = NSWorkspace.sharedWorkspace.frontmostApplication;
    if (!front) return false;
    const int frontPid = front.processIdentifier;
    if (frontPid == getpid()) return false;

    CGDirectDisplayID displays[16];
    uint32_t displayCount = 0;
    if (CGGetActiveDisplayList(16, displays, &displayCount) != kCGErrorSuccess || displayCount == 0) return false;

    CFArrayRef raw = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                kCGNullWindowID);
    if (!raw) return false;
    NSArray* list = CFBridgingRelease(raw);
    std::vector<CGRect> frontWindows;
    for (NSDictionary* w in list) {
        if ([w[(id)kCGWindowOwnerPID] intValue] != frontPid) continue;
        // Any non-negative level: games often raise a borderless window above
        // the menu bar; negative levels are desktop pictures and icons.
        if ([w[(id)kCGWindowLayer] intValue] < 0) continue;
        NSNumber* alpha = w[(id)kCGWindowAlpha];
        if (alpha && alpha.doubleValue < 0.05) continue;
        CGRect r;
        if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)w[(id)kCGWindowBounds], &r)) continue;
        frontWindows.push_back(r);
    }
    if (frontWindows.empty()) return false;

    // The app's windows together must cover a whole display (both in global
    // display points). Not just one window: full-screen Chrome is a tab strip,
    // a toolbar and the page, three windows stacked edge to edge. A zoomed
    // window stops at the menu bar and the Dock; tiled windows leave gaps.
    for (uint32_t i = 0; i < displayCount; ++i) {
        const CGRect display = CGDisplayBounds(displays[i]);
        std::vector<CGRect> parts;
        for (const CGRect& r : frontWindows) {
            const CGRect clipped = CGRectIntersection(r, display);
            if (!CGRectIsNull(clipped) && !CGRectIsEmpty(clipped)) parts.push_back(clipped);
        }
        const double displayArea = display.size.width * display.size.height;
        if (!parts.empty() && UnionArea(parts) >= displayArea * 0.999) return true;
    }
    return false;
}
