#include "windows_tracker/tracker.h"

#include <dwmapi.h>

#include <cwchar>

namespace {

WindowTracker* g_tracker = nullptr;  // hooks are process-wide; one tracker
constexpr ULONGLONG kFullRebuildMs = 500;

struct EnumContext {
    std::vector<TrackedWindow>* out;
    DWORD selfPid;
};

bool IsTaskbarClass(const wchar_t* cls) {
    return wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0;
}

bool IsTopLevelWindowEvent(HWND hwnd, LONG idObject, LONG idChild) {
    return hwnd != nullptr && idObject == OBJID_WINDOW && idChild == CHILDID_SELF &&
           GetAncestor(hwnd, GA_ROOT) == hwnd;
}

}  // namespace

bool WindowTracker::start() {
    stop();
    g_tracker = this;
    // One hook per event: a single wide range would also deliver the very
    // chatty ones (focus, name/value changes...) across processes.
    const DWORD events[] = {
        EVENT_SYSTEM_FOREGROUND,    EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
        EVENT_SYSTEM_MOVESIZEEND,   EVENT_OBJECT_SHOW,          EVENT_OBJECT_HIDE,
        EVENT_OBJECT_DESTROY,       EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_REORDER,
        EVENT_OBJECT_CLOAKED,       EVENT_OBJECT_UNCLOAKED,
    };
    for (DWORD e : events) {
        HWINEVENTHOOK h = SetWinEventHook(e, e, nullptr, &WindowTracker::OnWinEvent, 0, 0,
                                          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (h) hooks_.push_back(h);
    }
    dirty_ = true;
    return !hooks_.empty();
}

void WindowTracker::stop() {
    for (HWINEVENTHOOK h : hooks_) UnhookWinEvent(h);
    hooks_.clear();
    if (g_tracker == this) g_tracker = nullptr;
}

void CALLBACK WindowTracker::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject,
                                        LONG idChild, DWORD, DWORD) {
    if (!g_tracker) return;
    if (event == EVENT_SYSTEM_FOREGROUND || event == EVENT_OBJECT_REORDER) {
        g_tracker->dirty_ = true;  // z-order may have changed
        return;
    }
    // Cursor movement also fires LOCATIONCHANGE (OBJID_CURSOR, hwnd == null):
    // cheap early-out keeps that from costing anything.
    if (!IsTopLevelWindowEvent(hwnd, idObject, idChild)) return;
    g_tracker->dirty_ = true;
}

BOOL CALLBACK WindowTracker::OnEnumWindow(HWND hwnd, LPARAM param) {
    auto* ctx = reinterpret_cast<EnumContext*>(param);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->selfPid) return TRUE;  // our overlay / control window

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
        return TRUE;  // other virtual desktop, suspended UWP app, hidden shell UI...
    }

    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, 64);
    const bool taskbar = IsTaskbarClass(cls);
    if (!taskbar) {
        if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) return TRUE;  // desktop
        const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (ex & WS_EX_TOOLWINDOW) return TRUE;
        // Click-through overlays of other apps (game overlays, screen dimmers):
        // they cover everything but aren't real "ledges".
        if (ex & WS_EX_TRANSPARENT) return TRUE;
        if (ex & WS_EX_LAYERED) {
            BYTE alpha = 255;
            DWORD flags = 0;
            if (GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) &&
                alpha == 0) {
                return TRUE;
            }
        }
    }

    RECT r{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) {
        if (!GetWindowRect(hwnd, &r)) return TRUE;
    }
    if (r.right - r.left < 40 || r.bottom - r.top < 16) return TRUE;

    ctx->out->push_back({hwnd, r, taskbar});
    return TRUE;
}

bool WindowTracker::poll(std::vector<TrackedWindow>& out, bool force) {
    const ULONGLONG now = GetTickCount64();
    if (!force && !dirty_ && now - lastRebuild_ < kFullRebuildMs) return false;
    dirty_ = false;
    lastRebuild_ = now;
    out.clear();
    EnumContext ctx{&out, GetCurrentProcessId()};
    // EnumWindows walks top-level windows in z-order, topmost first.
    EnumWindows(&WindowTracker::OnEnumWindow, reinterpret_cast<LPARAM>(&ctx));
    return true;
}
