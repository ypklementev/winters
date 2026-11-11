#import <AppKit/AppKit.h>

#include "mac/controller.h"

int main(int, const char**) {
    @autoreleasepool {
        // One instance per user session.
        NSString* bundleId = NSBundle.mainBundle.bundleIdentifier;
        if (bundleId) {
            const pid_t self = NSProcessInfo.processInfo.processIdentifier;
            for (NSRunningApplication* app in [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleId]) {
                if (app.processIdentifier != self) return 0;
            }
        }

        NSApplication* app = NSApplication.sharedApplication;
        // No Dock icon or app menu (LSUIElement in Info.plist does the same for
        // the bundle; this also covers running the bare binary).
        app.activationPolicy = NSApplicationActivationPolicyAccessory;
        WTController* controller = [WTController new];
        app.delegate = controller;
        [app run];
    }
    return 0;
}
