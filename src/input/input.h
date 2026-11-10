// Cursor and keyboard polling (no hooks) and the global pause hotkey.
#pragma once

#include <windows.h>

struct PointerSample {
    bool valid = false;
    POINT pos{};           // physical px, screen coordinates
    bool sculptKeys = false;  // Ctrl + left Alt (not AltGr)
    bool primaryButton = false;
};

PointerSample SamplePointer();

// Ctrl+Alt+S, delivered as WM_HOTKEY with wParam == id.
bool RegisterPauseHotkey(HWND hwnd, int id);
void UnregisterPauseHotkey(HWND hwnd, int id);
