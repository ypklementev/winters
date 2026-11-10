// Tracks visible top-level windows (real DWM bounds, z-order) for the snow
// simulation. Updates are event-driven (SetWinEventHook) plus a periodic full
// rebuild as a safety net.
#pragma once

#include <windows.h>

#include <vector>

struct TrackedWindow {
    HWND hwnd = nullptr;
    RECT rect{};  // physical px, screen coordinates (no drop shadow)
    bool taskbar = false;
};

class WindowTracker {
public:
    WindowTracker() = default;
    WindowTracker(const WindowTracker&) = delete;
    WindowTracker& operator=(const WindowTracker&) = delete;
    ~WindowTracker() { stop(); }

    // Must be called from a thread with a message loop (out-of-context hooks
    // are delivered through it).
    bool start();
    void stop();

    // Rebuilds the list if an event arrived or the periodic interval elapsed.
    // Returns true if `out` was refreshed. Windows are ordered topmost first.
    bool poll(std::vector<TrackedWindow>& out, bool force = false);
    void markDirty() { dirty_ = true; }

private:
    static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                    DWORD, DWORD);
    static BOOL CALLBACK OnEnumWindow(HWND hwnd, LPARAM param);

    std::vector<HWINEVENTHOOK> hooks_;
    bool dirty_ = true;
    ULONGLONG lastRebuild_ = 0;
};
