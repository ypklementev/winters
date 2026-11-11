#include "overlay/overlay_window.h"

namespace {

constexpr wchar_t kClassName[] = L"WinTers.Overlay";

constexpr DWORD kExStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_LAYERED |
                           WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

bool RegisterOverlayClass(HINSTANCE instance, WNDPROC proc) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

}  // namespace

bool OverlayWindow::create(HINSTANCE instance, const RECT& r) {
    destroy();
    if (!RegisterOverlayClass(instance, &OverlayWindow::WndProc)) return false;

    rect_ = r;
    clickThrough_ = true;
    hwnd_ = CreateWindowExW(kExStyle, kClassName, L"WinTers", WS_POPUP, r.left, r.top,
                            r.right - r.left, r.bottom - r.top, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    // A layered window is invisible until its attributes are set. Alpha 255
    // keeps DirectComposition's per-pixel alpha intact.
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    visible_ = true;
    reassertTopmost();
    return true;
}

void OverlayWindow::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    visible_ = false;
}

void OverlayWindow::setClickThrough(bool on) {
    if (!hwnd_ || on == clickThrough_) return;
    clickThrough_ = on;
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    ex = on ? (ex | WS_EX_TRANSPARENT) : (ex & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT));
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void OverlayWindow::setVisible(bool visible) {
    if (!hwnd_ || visible == visible_) return;
    visible_ = visible;
    ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (visible) reassertTopmost();
}

void OverlayWindow::reassertTopmost() {
    if (!hwnd_ || !visible_) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_NCHITTEST:
            // Belt and braces on top of WS_EX_TRANSPARENT.
            if (!self || self->clickThrough_) return HTTRANSPARENT;
            return HTCLIENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_SETCURSOR:
            // Only reachable in sculpt mode (otherwise the window is click-through).
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
