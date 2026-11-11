// macOS counterpart of src/app/app.*: owns the menu bar item, the overlays,
// the renderer and the simulation, and drives the fixed-step loop.
#pragma once

#import <AppKit/AppKit.h>

@interface WTController : NSObject <NSApplicationDelegate, NSMenuDelegate>
@end
