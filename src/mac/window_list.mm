#include "mac/window_list.h"

#import <AppKit/AppKit.h>
#include <unistd.h>

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
    for (NSDictionary* w in list) {
        if ([w[(id)kCGWindowOwnerPID] intValue] != frontPid) continue;
        // Any non-negative level: games often raise a borderless window above
        // the menu bar; negative levels are desktop pictures and icons.
        if ([w[(id)kCGWindowLayer] intValue] < 0) continue;
        NSNumber* alpha = w[(id)kCGWindowAlpha];
        if (alpha && alpha.doubleValue < 0.05) continue;
        CGRect r;
        if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)w[(id)kCGWindowBounds], &r)) continue;
        // Both are global display coordinates in points. A zoomed (green
        // button + Option) window stops at the menu bar and the Dock.
        for (uint32_t i = 0; i < displayCount; ++i) {
            if (CGRectContainsRect(r, CGDisplayBounds(displays[i]))) return true;
        }
    }
    return false;
}
