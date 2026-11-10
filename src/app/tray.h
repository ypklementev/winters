// Notification-area icon. Menu handling lives in App.
#pragma once

#include <windows.h>

class TrayIcon {
public:
    TrayIcon() = default;
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;
    ~TrayIcon() { destroy(); }

    // `callbackMsg` is posted to `owner`; with NOTIFYICON_VERSION_4,
    // LOWORD(lParam) is the event and wParam holds the anchor point.
    bool create(HWND owner, UINT callbackMsg, HICON icon, const wchar_t* tooltip);
    // Explorer restarted ("TaskbarCreated"): add the icon again.
    bool recreate();
    void setTooltip(const wchar_t* tooltip);
    void destroy();

private:
    bool add();

    HWND owner_ = nullptr;
    UINT callbackMsg_ = 0;
    HICON icon_ = nullptr;
    wchar_t tooltip_[128]{};
    bool added_ = false;
};
