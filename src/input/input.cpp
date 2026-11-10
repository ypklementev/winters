#include "input/input.h"

namespace {
bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
}  // namespace

PointerSample SamplePointer() {
    PointerSample s;
    s.valid = GetCursorPos(&s.pos) != FALSE;
    // AltGr is reported as LeftCtrl + RightAlt, so require the *left* Alt and
    // no right Alt: typing on Polish/German layouts won't enter sculpt mode.
    const bool ctrl = Down(VK_LCONTROL) || Down(VK_RCONTROL);
    s.sculptKeys = ctrl && Down(VK_LMENU) && !Down(VK_RMENU);
    // GetAsyncKeyState reports physical buttons; honour "swap mouse buttons".
    s.primaryButton = Down(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON);
    return s;
}

bool RegisterPauseHotkey(HWND hwnd, int id) {
    return RegisterHotKey(hwnd, id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'S') != FALSE;
}

void UnregisterPauseHotkey(HWND hwnd, int id) { UnregisterHotKey(hwnd, id); }
