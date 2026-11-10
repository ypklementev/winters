// Snapshot of the monitor layout (physical px, Per-Monitor-V2 aware).
#pragma once

#include <windows.h>

#include <vector>

struct DisplayDesc {
    RECT bounds{};
    RECT work{};
    UINT dpi = 96;
};

struct DisplayLayout {
    RECT virtualRect{};  // SM_XVIRTUALSCREEN... in physical px
    std::vector<DisplayDesc> displays;
};

DisplayLayout QueryDisplayLayout();
