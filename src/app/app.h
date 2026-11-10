// Application: owns the control window, tray, overlay, renderer, window
// tracker and simulation, and drives the fixed-step loop.
#pragma once

#include <windows.h>

#include <vector>

#include "app/tray.h"
#include "overlay/overlay_window.h"
#include "render/renderer.h"
#include "sim/settings_model.h"
#include "sim/world.h"
#include "windows_tracker/tracker.h"

class App {
public:
    App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    ~App();

    int run(HINSTANCE instance);

private:
    enum PauseReason : unsigned {
        kPauseUser = 1u << 0,
        kPauseFullscreen = 1u << 1,
        kPauseBattery = 1u << 2,
        kPauseLocked = 1u << 3,
        kPauseSuspended = 1u << 4,
    };

    static LRESULT CALLBACK ControlProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleControl(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool createControlWindow();
    // (Re)creates overlay + renderer for the current monitor layout.
    HRESULT rebuildDisplays();
    void onDeviceLost();

    void tick();
    void scheduleNextTick();
    void updateWindows(bool force);
    void updatePointer(double elapsed);

    bool paused() const { return pauseReasons_ != 0; }
    void setPaused(unsigned reason, bool on);
    void checkAutoPause();
    void updateTooltip();

    void applySettings();
    void showTrayMenu(POINT at);
    void showHelp();

    HINSTANCE instance_ = nullptr;
    HWND control_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    HICON trayIcon_ = nullptr;
    TrayIcon tray_;
    OverlayWindow overlay_;
    Renderer renderer_;
    WindowTracker tracker_;
    sim::World world_;
    sim::UserSettings settings_;
    bool hotkeyOk_ = false;
    unsigned pauseReasons_ = 0;

    RECT virtualRect_{};
    std::vector<TrackedWindow> tracked_;
    std::vector<sim::WindowInfo> windows_;

    HANDLE tickTimer_ = nullptr;
    LARGE_INTEGER qpcFreq_{};
    LARGE_INTEGER lastTick_{};
    double accumulator_ = 0.0;
    bool framePending_ = false;
    bool prewarmed_ = false;

    POINT lastCursor_{};
    bool haveCursor_ = false;
    sim::Vec2 cursorVel_;
};
