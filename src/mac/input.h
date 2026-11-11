// Cursor and modifier polling, and the global pause hotkey. None of this needs
// the Accessibility or Input Monitoring permission.
#pragma once

#import <CoreGraphics/CoreGraphics.h>

#include <functional>

struct PointerSample {
    bool valid = false;
    CGPoint pos{};              // global display coordinates (points, y down)
    bool sculptKeys = false;    // Control + Option
    bool primaryButton = false;
};

PointerSample SamplePointer();

// Control+Option+S. `onPress` runs on the main thread.
class PauseHotkey {
public:
    PauseHotkey() = default;
    PauseHotkey(const PauseHotkey&) = delete;
    PauseHotkey& operator=(const PauseHotkey&) = delete;
    ~PauseHotkey() { unregister(); }

    bool registerHotkey(std::function<void()> onPress);
    void unregister();
    bool active() const { return hotkey_ != nullptr; }

private:
    void* hotkey_ = nullptr;   // EventHotKeyRef
    void* handler_ = nullptr;  // EventHandlerRef
    std::function<void()> onPress_;
};
