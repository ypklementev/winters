// Transparent, click-through window over one screen, above app windows and
// below the Dock and the menu bar, on every Space (including full-screen apps).
// A window can't span displays when "Displays have separate Spaces" is on,
// so there is one overlay per screen.
#pragma once

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "mac/metal_renderer.h"
#include "mac/screens.h"

@interface WTOverlay : NSObject

- (instancetype)initWithScreen:(const ScreenDesc&)screen
                        layout:(const ScreenLayout&)layout
                        device:(id<MTLDevice>)device;
- (void)close;

@property(nonatomic, readonly) CAMetalLayer* metalLayer;
// Part of the simulation space this overlay shows.
@property(nonatomic, readonly) MetalViewport viewport;

// NO: the overlay takes mouse input (over a snowball, in sculpt mode...).
- (void)setClickThrough:(BOOL)on;
- (void)setVisible:(BOOL)visible;
// Cursor shown while the overlay takes mouse input (nil = arrow).
- (void)setGrabCursor:(NSCursor*)cursor;

@end
