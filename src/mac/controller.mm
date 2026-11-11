#include "mac/controller.h"

#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "mac/input.h"
#include "mac/metal_renderer.h"
#include "mac/overlay.h"
#include "mac/prefs.h"
#include "mac/screens.h"
#include "mac/window_list.h"
#include "sim/settings_model.h"
#include "sim/world.h"

namespace {

enum PauseReason : unsigned {
    kPauseUser = 1u << 0,
    kPauseLowPower = 1u << 2,
    kPauseLocked = 1u << 3,
    kPauseSuspended = 1u << 4,
    kPauseScreensAsleep = 1u << 5,
};

enum MenuId : NSInteger {
    kMenuPause = 100,
    kMenuLight = 110,
    kMenuSteady,
    kMenuBlizzard,
    kMenuWindCalm = 120,
    kMenuWindLeft,
    kMenuWindRight,
    kMenuWindWeak = 130,
    kMenuWindMedium,
    kMenuWindStrong,
    kMenuShovel = 140,
    kMenuWiper = 145,
    kMenuUnlimited = 146,
    kMenuLoginItem = 151,
    kMenuHelp = 160,
    kMenuAuthor = 161,
    kMenuExit = 199,
};

// Window list polling: every tick while a mouse button is down (a window may
// be being dragged), otherwise every few ticks.
constexpr int kPollTicksDragging = 1;
constexpr int kPollTicksIdle = 3;

NSString* const kAuthorUrl = @"https://ypklementev.ru";

}  // namespace

@implementation WTController {
    sim::World _world;
    sim::UserSettings _settings;
    MetalRenderer _renderer;
    ScreenLayout _layout;
    NSMutableArray<WTOverlay*>* _overlays;
    NSStatusItem* _statusItem;
    PauseHotkey _hotkey;
    unsigned _pauseReasons;

    std::vector<MacWindow> _macWindows;
    std::vector<sim::WindowInfo> _windows;
    int _pollCountdown;

    id _displayLink;  // CADisplayLink (macOS 14+)
    NSTimer* _timer;  // fallback on macOS 13
    NSTimer* _rebuildTimer;
    double _lastTick;
    double _startTime;
    double _accumulator;
    bool _framePending;
    bool _prewarmed;

    CGPoint _lastCursor;
    bool _haveCursor;
    sim::Vec2 _cursorVel;
}

// ============================================================================
// Launch / quit
// ============================================================================

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    _overlays = [NSMutableArray array];
    _startTime = CACurrentMediaTime();

    _settings = LoadSettings();
    [self applySettings];

    std::string error;
    if (!_renderer.init(MTLCreateSystemDefaultDevice(), &error)) {
        [self activate];
        NSAlert* alert = [NSAlert new];
        alert.alertStyle = NSAlertStyleCritical;
        alert.messageText = @"WinTers";
        alert.informativeText = [NSString stringWithFormat:@"Не удалось инициализировать Metal.\n%s", error.c_str()];
        [alert runModal];
        [NSApp terminate:nil];
        return;
    }

    [self createStatusItem];
    __weak WTController* weakSelf = self;
    _hotkey.registerHotkey([weakSelf] { [weakSelf togglePause]; });
    [self observeSystem];

    [self rebuildDisplays];
    [self checkAutoPause];
    [self updateTooltip];
    _lastTick = CACurrentMediaTime();
    [self startTicking];
}

- (void)applicationWillTerminate:(NSNotification*)note {
    [self stopTicking];
    _hotkey.unregister();
    for (WTOverlay* o in _overlays) [o close];
    [_overlays removeAllObjects];
}

- (void)activate {
    if (@available(macOS 14.0, *)) {
        [NSApp activate];
    } else {
        [NSApp activateIgnoringOtherApps:YES];
    }
}

// ============================================================================
// Displays
// ============================================================================

- (void)rebuildDisplays {
    for (WTOverlay* o in _overlays) [o close];
    [_overlays removeAllObjects];

    _layout = ScreenLayout::query();
    if (_layout.empty()) return;  // e.g. lid closed with no external display; wait for the next change
    for (const ScreenDesc& d : _layout.screens()) {
        WTOverlay* o = [[WTOverlay alloc] initWithScreen:d layout:_layout device:_renderer.device()];
        if (self.paused) [o setVisible:NO];
        [_overlays addObject:o];
    }

    _world.setMonitors(_layout.monitors());
    [self updateWindows:YES pointerDown:false];
    [self placeWiper];
    if (!_prewarmed) {
        _world.prewarm();
        _prewarmed = true;
    }
    _haveCursor = false;
    _framePending = true;
    if (_displayLink || _timer) [self startTicking];  // the display link belongs to a screen
}

- (void)scheduleRebuildAfter:(NSTimeInterval)delay {
    [_rebuildTimer invalidate];
    __weak WTController* weakSelf = self;
    _rebuildTimer = [NSTimer scheduledTimerWithTimeInterval:delay
                                                    repeats:NO
                                                      block:^(NSTimer*) {
                                                        [weakSelf rebuildDisplays];
                                                      }];
}

// ============================================================================
// Frame loop
// ============================================================================

- (void)startTicking {
    [self stopTicking];
    if (self.paused) return;
    if (@available(macOS 14.0, *)) {
        NSScreen* screen = NSScreen.mainScreen ? NSScreen.mainScreen : NSScreen.screens.firstObject;
        if (screen) {
            CADisplayLink* link = [screen displayLinkWithTarget:self selector:@selector(onDisplayLink:)];
            link.preferredFrameRateRange = CAFrameRateRangeMake(30, 60, 60);
            // Common modes: keep snowing while a menu or an alert is open.
            [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
            _displayLink = link;
            return;
        }
    }
    _timer = [NSTimer timerWithTimeInterval:sim::kFixedDt target:self selector:@selector(onTimer:) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopTicking {
    if (@available(macOS 14.0, *)) {
        [(CADisplayLink*)_displayLink invalidate];
    }
    _displayLink = nil;
    [_timer invalidate];
    _timer = nil;
}

- (void)onDisplayLink:(id)link API_AVAILABLE(macos(14.0)) {
    // The vsync timestamp is steadier than "now", so frames get an even
    // number of simulation steps.
    [self tickAt:((CADisplayLink*)link).timestamp];
}

- (void)onTimer:(NSTimer*)timer {
    [self tickAt:CACurrentMediaTime()];
}

- (void)tickAt:(double)now {
    if (self.paused) return;
    @autoreleasepool {
        const double elapsed = std::max(now - _lastTick, 0.0);
        _lastTick = now;
        // Clamp so a long stall (sleep, debugger) doesn't fast-forward.
        _accumulator += std::min(elapsed, 0.25);

        const PointerSample pointer = SamplePointer();
        [self updateWindows:NO pointerDown:pointer.primaryButton];
        [self updatePointer:pointer elapsed:elapsed];

        while (_accumulator >= sim::kFixedDt) {
            _world.step(sim::kFixedDt);
            _accumulator -= sim::kFixedDt;
            _framePending = true;
        }
        if (_framePending) [self render];

        if (_world.takeWiperMoved()) {
            const CGPoint p = _layout.toGlobal(_world.wiper().pivot());
            _settings.wiperPlaced = true;
            _settings.wiperX = static_cast<int>(std::lround(p.x));
            _settings.wiperY = static_cast<int>(std::lround(p.y));
            SaveSettings(_settings);
        }
    }
}

- (void)render {
    _framePending = false;
    const float t = static_cast<float>(std::fmod(CACurrentMediaTime() - _startTime, 3600.0));
    id<MTLCommandBuffer> cb = _renderer.beginFrame(_world, t);
    if (!cb) return;
    for (WTOverlay* o in _overlays) {
        id<CAMetalDrawable> drawable = [o.metalLayer nextDrawable];
        if (!drawable) continue;
        _renderer.encode(cb, drawable.texture, o.viewport);
        [cb presentDrawable:drawable];
    }
    [cb commit];
}

- (void)updateWindows:(BOOL)force pointerDown:(bool)pointerDown {
    if (!force && --_pollCountdown > 0) return;
    _pollCountdown = pointerDown ? kPollTicksDragging : kPollTicksIdle;
    std::vector<MacWindow> list = QueryWindows();
    if (!force && list == _macWindows) return;
    _macWindows = std::move(list);

    _windows.clear();
    _layout.appendMenuBars(_windows);  // above every normal window
    const float radius = static_cast<float>(WindowCornerRadius() * _layout.scale());
    for (const MacWindow& w : _macWindows) {
        sim::WindowInfo info;
        info.id = w.id;
        info.rect = _layout.toSim(w.frame);
        info.cornerRadius = radius;
        _windows.push_back(info);
    }
    _world.setWindows(_windows);
}

- (void)updatePointer:(const PointerSample&)s elapsed:(double)elapsed {
    const sim::Vec2 local = _layout.toSim(s.pos);
    // The overlay takes mouse input only where there is something to grab, while
    // dragging it, or in sculpt mode (Control+Option). Everywhere else clicks go
    // to the windows underneath. A press that started elsewhere isn't stolen.
    const bool overGrabbable = s.valid && !s.primaryButton && _world.hitTest(local) != sim::HitKind::None;
    const bool holding = _world.holdingSomething();
    const bool capture = s.sculptKeys || holding || overGrabbable;
    NSCursor* cursor = holding ? NSCursor.closedHandCursor : NSCursor.openHandCursor;
    for (WTOverlay* o in _overlays) {
        [o setGrabCursor:cursor];
        [o setClickThrough:!capture];
    }

    sim::PointerInput p;
    p.valid = s.valid;
    if (s.valid) {
        if (_haveCursor && elapsed > 0.001) {
            const double k = _layout.scale() / elapsed;
            const sim::Vec2 raw{static_cast<float>((s.pos.x - _lastCursor.x) * k),
                                static_cast<float>((s.pos.y - _lastCursor.y) * k)};
            _cursorVel = _cursorVel * 0.4f + raw * 0.6f;
        }
        _lastCursor = s.pos;
        _haveCursor = true;
        p.pos = local;
        p.vel = _cursorVel;
        p.sculpt = s.sculptKeys;
        p.pressed = s.primaryButton;
    } else {
        _haveCursor = false;
    }
    _world.setPointer(p);
}

- (void)placeWiper {
    CGPoint at = CGPointMake(_settings.wiperX, _settings.wiperY);
    bool onScreen = false;
    for (const ScreenDesc& d : _layout.screens()) onScreen = onScreen || CGRectContainsPoint(d.frame, at);
    // A saved position that is no longer on any screen falls back to the default.
    if ((!_settings.wiperPlaced || !onScreen) && !_layout.empty()) {
        // Bottom centre of the main screen's work area, above the Dock.
        const CGRect v = _layout.screens()[0].visible;
        at = CGPointMake(CGRectGetMidX(v), CGRectGetMaxY(v) - 16.0);
    }
    _world.placeWiper(_layout.toSim(at));
    _world.setWiperEnabled(_settings.wiper);
}

- (void)applySettings {
    _settings = sim::sanitize(_settings);
    _world.setConfig(sim::toSnowConfig(_settings));
}

// ============================================================================
// Pause
// ============================================================================

- (BOOL)paused {
    return _pauseReasons != 0;
}

- (void)setPaused:(unsigned)reason on:(bool)on {
    const BOOL was = self.paused;
    _pauseReasons = on ? (_pauseReasons | reason) : (_pauseReasons & ~reason);
    if (self.paused != was) {
        if (self.paused) {
            [self stopTicking];
            for (WTOverlay* o in _overlays) {
                [o setClickThrough:YES];
                [o setVisible:NO];
            }
        } else {
            for (WTOverlay* o in _overlays) [o setVisible:YES];
            [self updateWindows:YES pointerDown:false];
            _lastTick = CACurrentMediaTime();
            _accumulator = 0.0;
            _haveCursor = false;
            _framePending = true;
            [self startTicking];
        }
    }
    [self updateTooltip];
}

- (void)togglePause {
    [self setPaused:kPauseUser on:!(_pauseReasons & kPauseUser)];
}

- (void)checkAutoPause {
    [self setPaused:kPauseLowPower on:NSProcessInfo.processInfo.lowPowerModeEnabled];
}

- (void)updateTooltip {
    NSString* state = !self.paused                        ? @"идёт снег"
                      : (_pauseReasons & kPauseUser)      ? @"пауза"
                      : (_pauseReasons & kPauseLowPower)  ? @"пауза: режим энергосбережения"
                                                          : @"пауза";
    _statusItem.button.toolTip = [@"WinTers — " stringByAppendingString:state];
    _statusItem.button.appearsDisabled = self.paused;
}

- (void)observeSystem {
    NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
    NSNotificationCenter* ws = NSWorkspace.sharedWorkspace.notificationCenter;
    NSDistributedNotificationCenter* dc = NSDistributedNotificationCenter.defaultCenter;
    __weak WTController* weakSelf = self;
    auto on = [](NSNotificationCenter* c, NSNotificationName name, void (^block)(void)) {
        [c addObserverForName:name
                       object:nil
                        queue:NSOperationQueue.mainQueue
                   usingBlock:^(NSNotification*) {
                     block();
                   }];
    };

    // Monitors added/removed/rearranged, resolution, Dock moved or resized.
    on(nc, NSApplicationDidChangeScreenParametersNotification, ^{
      [weakSelf scheduleRebuildAfter:0.4];
    });
    on(nc, NSProcessInfoPowerStateDidChangeNotification, ^{
      [weakSelf checkAutoPause];
    });
    on(ws, NSWorkspaceWillSleepNotification, ^{
      [weakSelf setPaused:kPauseSuspended on:true];
    });
    on(ws, NSWorkspaceDidWakeNotification, ^{
      [weakSelf setPaused:kPauseSuspended on:false];
      [weakSelf scheduleRebuildAfter:1.0];
    });
    on(ws, NSWorkspaceScreensDidSleepNotification, ^{
      [weakSelf setPaused:kPauseScreensAsleep on:true];
    });
    on(ws, NSWorkspaceScreensDidWakeNotification, ^{
      [weakSelf setPaused:kPauseScreensAsleep on:false];
    });
    // Fast user switching.
    on(ws, NSWorkspaceSessionDidResignActiveNotification, ^{
      [weakSelf setPaused:kPauseLocked on:true];
    });
    on(ws, NSWorkspaceSessionDidBecomeActiveNotification, ^{
      [weakSelf setPaused:kPauseLocked on:false];
    });
    on(ws, NSWorkspaceActiveSpaceDidChangeNotification, ^{
      [weakSelf windowsChanged];
    });
    on(ws, NSWorkspaceDidActivateApplicationNotification, ^{
      [weakSelf windowsChanged];
    });
    on(dc, @"com.apple.screenIsLocked", ^{
      [weakSelf setPaused:kPauseLocked on:true];
    });
    on(dc, @"com.apple.screenIsUnlocked", ^{
      [weakSelf setPaused:kPauseLocked on:false];
    });
}

- (void)windowsChanged {
    _pollCountdown = 0;  // re-read the window list on the next tick
}

// ============================================================================
// Menu bar item
// ============================================================================

- (void)createStatusItem {
    _statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength];
    NSImage* image = [NSImage imageWithSystemSymbolName:@"snowflake" accessibilityDescription:@"WinTers"];
    [image setTemplate:YES];  // `template` is a C++ keyword
    _statusItem.button.image = image;
    NSMenu* menu = [NSMenu new];
    menu.delegate = self;
    menu.autoenablesItems = NO;
    _statusItem.menu = menu;
}

- (NSMenuItem*)item:(NSString*)title tag:(NSInteger)tag {
    NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:title action:@selector(onMenu:) keyEquivalent:@""];
    it.target = self;
    it.tag = tag;
    return it;
}

- (NSMenuItem*)radio:(NSString*)title tag:(NSInteger)tag on:(bool)on {
    NSMenuItem* it = [self item:title tag:tag];
    it.state = on ? NSControlStateValueOn : NSControlStateValueOff;
    return it;
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
    if (menu != _statusItem.menu) return;
    [menu removeAllItems];

    NSString* title = (_pauseReasons & kPauseLowPower) ? @"WinTers — пауза (энергосбережение)" : @"WinTers";
    NSMenuItem* header = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    header.enabled = NO;
    [menu addItem:header];
    [menu addItem:NSMenuItem.separatorItem];

    NSMenuItem* pause = [self item:(_pauseReasons & kPauseUser) ? @"Продолжить" : @"Пауза" tag:kMenuPause];
    if (_hotkey.active()) {
        pause.keyEquivalent = @"s";
        pause.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagOption;
    }
    [menu addItem:pause];

    NSMenu* snow = [NSMenu new];
    snow.autoenablesItems = NO;
    [snow addItem:[self radio:@"Лёгкий" tag:kMenuLight on:_settings.intensity == sim::Intensity::Light]];
    [snow addItem:[self radio:@"Ровный" tag:kMenuSteady on:_settings.intensity == sim::Intensity::Steady]];
    [snow addItem:[self radio:@"Метель" tag:kMenuBlizzard on:_settings.intensity == sim::Intensity::Blizzard]];
    [snow addItem:NSMenuItem.separatorItem];
    [snow addItem:[self radio:@"Без ограничений (сугробы до верха экрана)" tag:kMenuUnlimited on:_settings.unlimited]];
    NSMenuItem* snowItem = [[NSMenuItem alloc] initWithTitle:@"Снегопад" action:nil keyEquivalent:@""];
    snowItem.submenu = snow;
    [menu addItem:snowItem];

    NSMenu* wind = [NSMenu new];
    wind.autoenablesItems = NO;
    const sim::WindDirection dir = _settings.windDirection;
    [wind addItem:[self radio:@"Штиль" tag:kMenuWindCalm on:dir == sim::WindDirection::Calm]];
    [wind addItem:[self radio:@"Влево" tag:kMenuWindLeft on:dir == sim::WindDirection::Left]];
    [wind addItem:[self radio:@"Вправо" tag:kMenuWindRight on:dir == sim::WindDirection::Right]];
    [wind addItem:NSMenuItem.separatorItem];
    NSString* const strengths[] = {@"Слабый", @"Средний", @"Сильный"};
    for (int i = 0; i < 3; ++i) {
        NSMenuItem* it = [self radio:strengths[i] tag:kMenuWindWeak + i on:_settings.windStrength == i + 1];
        it.enabled = dir != sim::WindDirection::Calm;
        [wind addItem:it];
    }
    NSMenuItem* windItem = [[NSMenuItem alloc] initWithTitle:@"Ветер" action:nil keyEquivalent:@""];
    windItem.submenu = wind;
    [menu addItem:windItem];

    [menu addItem:[self radio:@"Дворник" tag:kMenuWiper on:_settings.wiper]];
    [menu addItem:[self item:@"Лопата: убрать весь снег" tag:kMenuShovel]];
    [menu addItem:NSMenuItem.separatorItem];

    NSMenuItem* login = [self item:@"Открывать при входе в систему" tag:kMenuLoginItem];
    switch (GetLoginItemState()) {
        case LoginItemState::Enabled:
            login.state = NSControlStateValueOn;
            break;
        case LoginItemState::Disabled:
            break;
        case LoginItemState::RequiresApproval:
            login.title = @"Открытие при входе выключено в Системных настройках…";
            break;
        case LoginItemState::Unavailable:
            login.title = @"Открытие при входе недоступно";
            login.enabled = NO;
            break;
    }
    [menu addItem:login];
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItem:[self item:@"Как пользоваться…" tag:kMenuHelp]];
    [menu addItem:[self item:@"Автор: ypklementev.ru" tag:kMenuAuthor]];
    NSMenuItem* quit = [self item:@"Выйти" tag:kMenuExit];
    quit.keyEquivalent = @"q";
    [menu addItem:quit];
}

- (void)onMenu:(NSMenuItem*)sender {
    const NSInteger cmd = sender.tag;
    bool settingsChanged = true;
    switch (cmd) {
        case kMenuPause:
            [self togglePause];
            settingsChanged = false;
            break;
        case kMenuLight:
        case kMenuSteady:
        case kMenuBlizzard:
            _settings.intensity = static_cast<sim::Intensity>(cmd - kMenuLight);
            break;
        case kMenuWindCalm:
        case kMenuWindLeft:
        case kMenuWindRight:
            _settings.windDirection = static_cast<sim::WindDirection>(cmd - kMenuWindCalm);
            break;
        case kMenuWindWeak:
        case kMenuWindMedium:
        case kMenuWindStrong:
            _settings.windStrength = static_cast<int>(cmd - kMenuWindWeak) + 1;
            break;
        case kMenuShovel:
            _world.clear();
            _framePending = true;
            settingsChanged = false;
            break;
        case kMenuUnlimited:
            _settings.unlimited = !_settings.unlimited;
            break;
        case kMenuWiper:
            _settings.wiper = !_settings.wiper;
            [self placeWiper];
            break;
        case kMenuLoginItem:
            switch (GetLoginItemState()) {
                case LoginItemState::Enabled:
                    SetLoginItem(false);
                    break;
                case LoginItemState::Disabled:
                    SetLoginItem(true);
                    break;
                case LoginItemState::RequiresApproval:
                    OpenLoginItemsSettings();
                    break;
                case LoginItemState::Unavailable:
                    break;
            }
            settingsChanged = false;
            break;
        case kMenuAuthor:
            [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:kAuthorUrl]];
            settingsChanged = false;
            break;
        case kMenuHelp:
            [self showHelp];
            settingsChanged = false;
            break;
        case kMenuExit:
            [NSApp terminate:nil];
            return;
        default:
            settingsChanged = false;
            break;
    }
    if (settingsChanged) {
        [self applySettings];
        SaveSettings(_settings);
    }
}

- (void)showHelp {
    [self activate];
    NSAlert* alert = [NSAlert new];
    alert.messageText = @"WinTers — как пользоваться";
    alert.informativeText =
        @"Снег падает и копится на верхних краях окон и внизу экрана, за Dock.\n\n"
        @"• Смахнуть снег — проведите курсором по сугробу (быстрый взмах сбивает и снежки).\n\n"
        @"• Снежный ком — зажмите Control + Option и ведите курсор вдоль сугроба: снег сгребается "
        @"в шарик, который катится перед курсором и растёт. Можно и нажать на сугроб с Control + Option "
        @"и катить снежок мышкой.\n\n"
        @"• Снеговик — наведите курсор на шарик (появится рука), возьмите и поставьте на другой или вплотную рядом: "
        @"шары слипаются, лепить можно сколько угодно шаров. Три шара от большого к маленькому — и снеговик получит "
        @"ведро, морковку и глаза. За нижний шар можно унести весь снеговик.\n\n"
        @"• Шар, брошенный или упавший с высоты, разбивается.\n\n"
        @"• «Снегопад → Без ограничений» — сугробы растут хоть до верха экрана.\n\n"
        @"• Дворник (в меню) смахивает снег. Его ось можно перетащить мышкой.\n\n"
        @"• Control + Option + S — пауза / продолжить.\n\n"
        @"• «Лопата» в меню убирает весь снег.\n\n"
        @"Автор: ypklementev.ru";
    [alert runModal];
}

@end
