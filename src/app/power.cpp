#include "app/power.h"

#include <windows.h>

#include <dwmapi.h>

#include <cwchar>

bool IsBatterySaverActive() {
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps)) return false;
    // SystemStatusFlag: 1 = battery saver on (Windows 10+). ACLineStatus 0 = on battery.
    return ps.SystemStatusFlag == 1 && ps.ACLineStatus == 0;
}

namespace {

// Shell surfaces that cover a whole monitor without being an "app": the
// desktop, the taskbar, Task View / Alt+Tab.
bool IsShellClass(const wchar_t* cls) {
    static constexpr const wchar_t* kShell[] = {
        L"Progman",
        L"WorkerW",
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"MultitaskingViewFrame",          // Alt+Tab / Task View, Windows 10
        L"TaskSwitcherWnd",                // classic Alt+Tab
        L"XamlExplorerHostIslandWindow",   // Alt+Tab / Task View / Snap, Windows 11
        L"ForegroundStaging",              // transient while switching apps
    };
    for (const wchar_t* s : kShell) {
        if (wcscmp(cls, s) == 0) return true;
    }
    return false;
}

}  // namespace

bool IsFullscreenAppForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    fg = GetAncestor(fg, GA_ROOT);
    if (!fg || !IsWindowVisible(fg) || IsIconic(fg)) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return false;  // our tray menu / message box

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(fg, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;

    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    if (IsShellClass(cls)) return false;

    // A maximized window with a title bar is just a big window, even when an
    // auto-hidden taskbar lets it reach the monitor edges.
    const LONG_PTR style = GetWindowLongPtrW(fg, GWL_STYLE);
    if (IsZoomed(fg) && (style & WS_CAPTION) == WS_CAPTION) return false;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)) return false;

    // Client area, not the window rect: a maximized window's frame overhangs
    // the monitor by its border width on every side.
    RECT client{};
    if (!GetClientRect(fg, &client)) return false;
    POINT tl{client.left, client.top};
    POINT br{client.right, client.bottom};
    if (!ClientToScreen(fg, &tl) || !ClientToScreen(fg, &br)) return false;
    return tl.x <= mi.rcMonitor.left && tl.y <= mi.rcMonitor.top && br.x >= mi.rcMonitor.right &&
           br.y >= mi.rcMonitor.bottom;
}
