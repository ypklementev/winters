#include "overlay/displays.h"

#include <shellscalingapi.h>

namespace {

BOOL CALLBACK EnumMonitor(HMONITOR mon, HDC, LPRECT, LPARAM param) {
    auto* out = reinterpret_cast<std::vector<DisplayDesc>*>(param);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return TRUE;
    DisplayDesc d;
    d.bounds = mi.rcMonitor;
    d.work = mi.rcWork;
    UINT dx = 96, dy = 96;
    if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) d.dpi = dx;
    out->push_back(d);
    return TRUE;
}

}  // namespace

DisplayLayout QueryDisplayLayout() {
    DisplayLayout layout;
    layout.virtualRect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    layout.virtualRect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    layout.virtualRect.right = layout.virtualRect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    layout.virtualRect.bottom = layout.virtualRect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitor,
                        reinterpret_cast<LPARAM>(&layout.displays));
    return layout;
}
