#include "mac/window_list.h"

#import <Foundation/Foundation.h>
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
