// Transparent, click-through, always-on-top window covering the whole virtual
// screen. Content comes from DirectComposition (no redirection bitmap).
#pragma once

#include <windows.h>

class OverlayWindow {
public:
    OverlayWindow() = default;
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;
    ~OverlayWindow() { destroy(); }

    bool create(HINSTANCE instance, const RECT& virtualRect);
    void destroy();

    HWND hwnd() const { return hwnd_; }
    const RECT& rect() const { return rect_; }

    // When false, the overlay receives mouse input (sculpt mode).
    void setClickThrough(bool on);
    bool clickThrough() const { return clickThrough_; }

    // Hidden while paused (e.g. a full-screen game is running).
    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // Topmost windows can be pushed down by other topmost ones (e.g. the taskbar).
    void reassertTopmost();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    HWND hwnd_ = nullptr;
    RECT rect_{};
    bool clickThrough_ = true;
    bool visible_ = false;
};
