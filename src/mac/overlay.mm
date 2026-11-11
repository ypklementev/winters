#include "mac/overlay.h"

// ---------------------------------------------------------------------------
// Panel: borderless, never key/main, not constrained below the menu bar.
// ---------------------------------------------------------------------------
@interface WTOverlayPanel : NSPanel
@end

@implementation WTOverlayPanel
- (BOOL)canBecomeKeyWindow {
    return NO;
}
- (BOOL)canBecomeMainWindow {
    return NO;
}
- (NSRect)constrainFrameRect:(NSRect)frameRect toScreen:(NSScreen*)screen {
    return frameRect;  // cover the menu bar too
}
@end

// ---------------------------------------------------------------------------
// View: hosts the CAMetalLayer, swallows clicks while not click-through and
// shows the grab cursor.
// ---------------------------------------------------------------------------
@interface WTOverlayView : NSView
@property(nonatomic, strong) NSCursor* grabCursor;
- (void)applyCursor;
@end

@implementation WTOverlayView

- (CALayer*)makeBackingLayer {
    return [CAMetalLayer layer];
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    return YES;
}

- (BOOL)isOpaque {
    return NO;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea* a in self.trackingAreas) [self removeTrackingArea:a];
    // ActiveAlways: we are a background app and never become active.
    const NSTrackingAreaOptions opts = NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                                       NSTrackingActiveAlways | NSTrackingInVisibleRect;
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect options:opts owner:self userInfo:nil]];
}

- (void)applyCursor {
    if (self.window.ignoresMouseEvents) return;
    [(self.grabCursor ? self.grabCursor : NSCursor.arrowCursor) set];
}

- (void)mouseEntered:(NSEvent*)event {
    [self applyCursor];
}
- (void)mouseMoved:(NSEvent*)event {
    [self applyCursor];
}
- (void)mouseDown:(NSEvent*)event {
    [self applyCursor];
}
- (void)mouseDragged:(NSEvent*)event {
    [self applyCursor];
}
- (void)mouseUp:(NSEvent*)event {
    [self applyCursor];
}
// Other buttons and the wheel fall through to nothing while we hold the
// mouse; that only happens over a snowball or in sculpt mode.
- (void)rightMouseDown:(NSEvent*)event {
}
- (void)otherMouseDown:(NSEvent*)event {
}

@end

// ---------------------------------------------------------------------------

@implementation WTOverlay {
    WTOverlayPanel* _panel;
    WTOverlayView* _view;
    BOOL _clickThrough;
    BOOL _visible;
}

- (instancetype)initWithScreen:(const ScreenDesc&)screen
                        layout:(const ScreenLayout&)layout
                        device:(id<MTLDevice>)device {
    if (!(self = [super init])) return nil;
    const NSRect frame = ScreenLayout::globalToCocoa(screen.frame);
    _panel = [[WTOverlayPanel alloc] initWithContentRect:frame
                                               styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    _panel.releasedWhenClosed = NO;
    _panel.opaque = NO;
    _panel.backgroundColor = NSColor.clearColor;
    _panel.hasShadow = NO;
    _panel.ignoresMouseEvents = YES;
    _panel.hidesOnDeactivate = NO;
    _panel.animationBehavior = NSWindowAnimationBehaviorNone;
    // Right below the Dock (20): above every app window, while the Dock, the
    // menu bar, menus and Control Center stay in front of the snow, so a drift
    // at the screen bottom never buries the Dock icons.
    _panel.level = CGWindowLevelForKey(kCGDockWindowLevelKey) - 1;
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
                                NSWindowCollectionBehaviorIgnoresCycle | NSWindowCollectionBehaviorFullScreenAuxiliary;
    // Experiment hooks (Space-switch behaviour can only be checked by eye):
    //   defaults write <bundle id> DebugOverlayLevel -int <level>
    //   defaults write <bundle id> DebugOverlayBehavior -int <NSWindowCollectionBehavior bits>
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    if (id level = [defaults objectForKey:@"DebugOverlayLevel"]) _panel.level = [level integerValue];
    if (id behavior = [defaults objectForKey:@"DebugOverlayBehavior"]) {
        @try {
            _panel.collectionBehavior = static_cast<NSWindowCollectionBehavior>([behavior unsignedIntegerValue]);
        } @catch (NSException* e) {
            NSLog(@"WinTers: DebugOverlayBehavior %@ rejected: %@", behavior, e.reason);
        }
    }
    _clickThrough = YES;

    _view = [[WTOverlayView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)];
    _view.wantsLayer = YES;
    _panel.contentView = _view;
    [_panel setFrame:frame display:NO];

    _metalLayer = (CAMetalLayer*)_view.layer;
    _metalLayer.device = device;
    _metalLayer.pixelFormat = MetalRenderer::kPixelFormat;
    _metalLayer.opaque = NO;
    _metalLayer.framebufferOnly = YES;
    _metalLayer.contentsScale = screen.backing;
    _metalLayer.drawableSize = CGSizeMake(frame.size.width * screen.backing, frame.size.height * screen.backing);
    CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    _metalLayer.colorspace = srgb;
    CGColorSpaceRelease(srgb);

    const sim::RectF r = layout.toSim(screen.frame);
    _viewport.originX = r.left;
    _viewport.originY = r.top;
    _viewport.width = r.width();
    _viewport.height = r.height();
    _viewport.unitsPerPixel = static_cast<float>(layout.scale() / screen.backing);

    [self setVisible:YES];
    return self;
}

- (void)close {
    [_panel orderOut:nil];
    [_panel close];
    _panel = nil;
}

- (void)setClickThrough:(BOOL)on {
    if (on == _clickThrough) return;
    _clickThrough = on;
    _panel.ignoresMouseEvents = on;
    if (!on) [_view applyCursor];
}

- (void)setVisible:(BOOL)visible {
    if (visible == _visible) return;
    _visible = visible;
    if (visible) {
        [_panel orderFrontRegardless];
    } else {
        [_panel orderOut:nil];
    }
}

- (void)setGrabCursor:(NSCursor*)cursor {
    if (_view.grabCursor == cursor) return;
    _view.grabCursor = cursor;
    [_view applyCursor];
}

@end
