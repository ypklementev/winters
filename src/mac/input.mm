#include "mac/input.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include "mac/screens.h"

PointerSample SamplePointer() {
    PointerSample s;
    s.valid = NSScreen.screens.count > 0;
    s.pos = ScreenLayout::cocoaToGlobal(NSEvent.mouseLocation);
    const CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
    // Macs have no AltGr, so either Option key will do.
    s.sculptKeys = (flags & kCGEventFlagMaskControl) && (flags & kCGEventFlagMaskAlternate);
    s.primaryButton = (NSEvent.pressedMouseButtons & 1) != 0;
    return s;
}

namespace {

constexpr OSType kSignature = 'WnTr';

OSStatus OnHotkey(EventHandlerCallRef, EventRef, void* user) {
    auto* fn = static_cast<std::function<void()>*>(user);
    if (*fn) (*fn)();
    return noErr;
}

}  // namespace

bool PauseHotkey::registerHotkey(std::function<void()> onPress) {
    unregister();
    onPress_ = std::move(onPress);
    const EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
    EventHandlerRef handler = nullptr;
    if (InstallApplicationEventHandler(&OnHotkey, 1, &spec, &onPress_, &handler) != noErr) return false;
    handler_ = handler;
    EventHotKeyRef ref = nullptr;
    const EventHotKeyID id{kSignature, 1};
    if (RegisterEventHotKey(kVK_ANSI_S, controlKey | optionKey, id, GetApplicationEventTarget(), 0, &ref) != noErr) {
        unregister();
        return false;
    }
    hotkey_ = ref;
    return true;
}

void PauseHotkey::unregister() {
    if (hotkey_) UnregisterEventHotKey(static_cast<EventHotKeyRef>(hotkey_));
    if (handler_) RemoveEventHandler(static_cast<EventHandlerRef>(handler_));
    hotkey_ = nullptr;
    handler_ = nullptr;
}
