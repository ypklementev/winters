#include "app/tray.h"

#include <shellapi.h>

#include <cwchar>

namespace {
constexpr UINT kIconId = 1;
}

bool TrayIcon::create(HWND owner, UINT callbackMsg, HICON icon, const wchar_t* tooltip) {
    owner_ = owner;
    callbackMsg_ = callbackMsg;
    icon_ = icon;
    wcsncpy_s(tooltip_, ARRAYSIZE(tooltip_), tooltip, _TRUNCATE);
    return add();
}

bool TrayIcon::add() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner_;
    nid.uID = kIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = callbackMsg_;
    nid.hIcon = icon_;
    wcsncpy_s(nid.szTip, ARRAYSIZE(nid.szTip), tooltip_, _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    added_ = true;
    return true;
}

bool TrayIcon::recreate() {
    added_ = false;
    return add();
}

void TrayIcon::setTooltip(const wchar_t* tooltip) {
    wcsncpy_s(tooltip_, ARRAYSIZE(tooltip_), tooltip, _TRUNCATE);
    if (!added_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner_;
    nid.uID = kIconId;
    nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(nid.szTip, ARRAYSIZE(nid.szTip), tooltip_, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::destroy() {
    if (!added_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner_;
    nid.uID = kIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    added_ = false;
}
