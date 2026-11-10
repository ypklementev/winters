#include "app/app.h"

#include <shellapi.h>
#include <windowsx.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "app/autostart.h"
#include "app/icon.h"
#include "app/power.h"
#include "app/settings.h"
#include "input/input.h"
#include "overlay/displays.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace {

constexpr wchar_t kControlClass[] = L"WinFlake.Control";
constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr int kHotkeyPause = 1;

enum TimerId : UINT_PTR {
    kTimerTopmost = 1,    // periodically re-assert overlay z-order
    kTimerDisplay = 2,    // debounce display changes / resume
    kTimerRecover = 3,    // retry renderer creation after device loss
    kTimerModalTick = 4,  // keep animating while a menu/message box runs its modal loop
    kTimerAutoPause = 5,  // poll battery saver state
};

enum MenuId : UINT {
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
    kMenuAutostart = 151,
    kMenuHelp = 160,
    kMenuExit = 199,
};

sim::RectF ToLocal(const RECT& r, const RECT& origin) {
    return {static_cast<float>(r.left - origin.left), static_cast<float>(r.top - origin.top),
            static_cast<float>(r.right - origin.left), static_cast<float>(r.bottom - origin.top)};
}

}  // namespace

App::~App() {
    tracker_.stop();
    renderer_.shutdown();
    overlay_.destroy();
    tray_.destroy();
    if (trayIcon_) DestroyIcon(trayIcon_);
    if (tickTimer_) CloseHandle(tickTimer_);
    if (control_) DestroyWindow(control_);
}

// ============================================================================
// Setup
// ============================================================================

bool App::createControlWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &App::ControlProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kControlClass;
    if (!RegisterClassExW(&wc)) return false;
    // A real (hidden) top-level window rather than HWND_MESSAGE: it must be able
    // to become foreground for TrackPopupMenu and receive broadcast messages.
    control_ = CreateWindowExW(WS_EX_TOOLWINDOW, kControlClass, L"WinFlake", WS_POPUP, 0, 0, 0, 0,
                               nullptr, nullptr, instance_, this);
    return control_ != nullptr;
}

HRESULT App::rebuildDisplays() {
    renderer_.shutdown();
    overlay_.destroy();

    const DisplayLayout layout = QueryDisplayLayout();
    virtualRect_ = layout.virtualRect;
    if (!overlay_.create(instance_, virtualRect_)) {
        const DWORD err = GetLastError();
        return err ? HRESULT_FROM_WIN32(err) : E_FAIL;
    }
    if (paused()) overlay_.setVisible(false);

    // Simulation space = overlay client space (virtual screen shifted to 0,0).
    std::vector<sim::Monitor> monitors;
    for (const auto& d : layout.displays) {
        sim::Monitor m;
        m.bounds = ToLocal(d.bounds, virtualRect_);
        m.work = ToLocal(d.work, virtualRect_);
        m.scale = static_cast<float>(d.dpi) / 96.0f;
        monitors.push_back(m);
    }
    world_.setMonitors(std::move(monitors));
    updateWindows(true);
    placeWiper();
    if (!prewarmed_) {
        world_.prewarm();
        prewarmed_ = true;
    }

    const HRESULT hr =
        renderer_.init(overlay_.hwnd(), static_cast<UINT>(virtualRect_.right - virtualRect_.left),
                       static_cast<UINT>(virtualRect_.bottom - virtualRect_.top));
    framePending_ = true;
    return hr;
}

void App::onDeviceLost() {
    renderer_.shutdown();
    SetTimer(control_, kTimerRecover, 500, nullptr);
}

void App::applySettings() {
    settings_ = sim::sanitize(settings_);
    world_.setConfig(sim::toSnowConfig(settings_));
}

// ============================================================================
// Frame loop
// ============================================================================

void App::updateWindows(bool force) {
    if (!tracker_.poll(tracked_, force)) return;
    windows_.clear();
    windows_.reserve(tracked_.size());
    for (const TrackedWindow& t : tracked_) {
        sim::WindowInfo w;
        w.id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(t.hwnd));
        w.rect = ToLocal(t.rect, virtualRect_);
        w.taskbar = t.taskbar;
        windows_.push_back(w);
    }
    world_.setWindows(windows_);
}

void App::updatePointer(double elapsed) {
    const PointerSample s = SamplePointer();
    const sim::Vec2 local{static_cast<float>(s.pos.x - virtualRect_.left), static_cast<float>(s.pos.y - virtualRect_.top)};
    // The overlay takes mouse input only where there is something to grab
    // (a snowball, the wiper's pivot), while dragging it, or in sculpt mode
    // (Ctrl+Alt). Everywhere else clicks go to the windows underneath. A press
    // that started elsewhere (e.g. dragging a window over a ball) isn't stolen.
    const bool overGrabbable = s.valid && !s.primaryButton && world_.hitTest(local) != sim::HitKind::None;
    overlay_.setClickThrough(!(s.sculptKeys || world_.holdingSomething() || overGrabbable));

    sim::PointerInput p;
    p.valid = s.valid;
    if (s.valid) {
        if (haveCursor_ && elapsed > 0.001) {
            const sim::Vec2 raw{static_cast<float>((s.pos.x - lastCursor_.x) / elapsed),
                                static_cast<float>((s.pos.y - lastCursor_.y) / elapsed)};
            cursorVel_ = cursorVel_ * 0.4f + raw * 0.6f;
        }
        lastCursor_ = s.pos;
        haveCursor_ = true;
        p.pos = local;
        p.vel = cursorVel_;
        p.sculpt = s.sculptKeys;
        p.pressed = s.primaryButton;
    } else {
        haveCursor_ = false;
    }
    world_.setPointer(p);
}

void App::tick() {
    if (paused()) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const double elapsed =
        static_cast<double>(now.QuadPart - lastTick_.QuadPart) / static_cast<double>(qpcFreq_.QuadPart);
    lastTick_ = now;
    // Clamp so a long stall (sleep, debugger, modal loop) doesn't fast-forward.
    accumulator_ += std::min(elapsed, 0.25);

    updateWindows(false);
    updatePointer(elapsed);

    while (accumulator_ >= sim::kFixedDt) {
        world_.step(sim::kFixedDt);
        accumulator_ -= sim::kFixedDt;
        framePending_ = true;
    }

    // Render at most once per simulation step, and only if the swap chain can
    // take a frame right now (otherwise try again on the next tick).
    if (framePending_ && renderer_.ready() &&
        WaitForSingleObject(renderer_.frameWaitable(), 0) == WAIT_OBJECT_0) {
        framePending_ = false;
        if (renderer_.render(world_) != Renderer::Result::Ok) onDeviceLost();
    }
    if (world_.takeWiperMoved()) {
        const sim::Vec2 pv = world_.wiper().pivot();
        settings_.wiperPlaced = true;
        settings_.wiperX = static_cast<int>(pv.x) + virtualRect_.left;
        settings_.wiperY = static_cast<int>(pv.y) + virtualRect_.top;
        SaveSettings(settings_);
    }
    scheduleNextTick();
}

void App::placeWiper() {
    POINT at{settings_.wiperX, settings_.wiperY};
    // A saved position that is no longer on any monitor falls back to the default.
    if (!settings_.wiperPlaced || !MonitorFromPoint(at, MONITOR_DEFAULTTONULL)) {
        HMONITOR primary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(primary, &mi);
        const float scale = static_cast<float>(GetDpiForWindow(control_)) / 96.0f;
        // Bottom centre of the primary work area, cap fully visible above the taskbar.
        at = {(mi.rcWork.left + mi.rcWork.right) / 2, mi.rcWork.bottom - static_cast<LONG>(16.0f * scale)};
    }
    world_.placeWiper({static_cast<float>(at.x - virtualRect_.left), static_cast<float>(at.y - virtualRect_.top)});
    world_.setWiperEnabled(settings_.wiper);
}

void App::scheduleNextTick() {
    if (paused()) return;
    const double untilNext = std::max(sim::kFixedDt - accumulator_, 0.0005);
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(untilNext * 1e7);  // relative, 100 ns units
    SetWaitableTimer(tickTimer_, &due, 0, nullptr, nullptr, FALSE);
}

// ============================================================================
// Pause handling
// ============================================================================

void App::setPaused(unsigned reason, bool on) {
    const bool wasPaused = paused();
    pauseReasons_ = on ? (pauseReasons_ | reason) : (pauseReasons_ & ~reason);
    if (paused() == wasPaused) {
        updateTooltip();
        return;
    }
    if (paused()) {
        CancelWaitableTimer(tickTimer_);
        overlay_.setClickThrough(true);
        overlay_.setVisible(false);
    } else {
        overlay_.setVisible(true);
        updateWindows(true);
        QueryPerformanceCounter(&lastTick_);
        accumulator_ = 0.0;
        haveCursor_ = false;
        framePending_ = true;
        scheduleNextTick();
    }
    updateTooltip();
}

void App::checkAutoPause() {
    setPaused(kPauseBattery, IsBatterySaverActive());
}

void App::updateTooltip() {
    std::wstring tip = L"WinFlake — ";
    if (!paused()) {
        tip += L"идёт снег";
    } else if (pauseReasons_ & kPauseUser) {
        tip += L"пауза";
    } else if (pauseReasons_ & kPauseBattery) {
        tip += L"пауза: режим энергосбережения";
    } else {
        tip += L"пауза";
    }
    tray_.setTooltip(tip.c_str());
}

// ============================================================================
// Tray menu
// ============================================================================

void App::showHelp() {
    SetTimer(control_, kTimerModalTick, 15, nullptr);
    MessageBoxW(nullptr,
                L"Снег падает и копится на верхних краях окон, на панели задач и внизу экрана.\n\n"
                L"• Смахнуть снег — быстро махните курсором по сугробу.\n\n"
                L"• Снежный ком — неспешно ведите курсор вдоль сугроба: снег сгребается в шарик, "
                L"который катится перед курсором и растёт.\n\n"
                L"• Снеговик — наведите курсор на шарик (появится рука), нажмите и перетащите его. "
                L"Поставьте маленький шарик на большой.\n\n"
                L"• Слепить снежок прямо из сугроба — зажмите Ctrl + левый Alt, нажмите на сугроб и катите. "
                L"Пока зажаты Ctrl + Alt, клики попадают в снег, а не в окна.\n\n"
                L"• Дворник (в меню) смахивает снег. Его ось можно перетащить мышкой.\n\n"
                L"• Ctrl + Alt + S — пауза / продолжить.\n\n"
                L"• «Лопата» в меню убирает весь снег.",
                L"WinFlake — как пользоваться", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    KillTimer(control_, kTimerModalTick);
}

void App::showTrayMenu(POINT at) {
    HMENU menu = CreatePopupMenu();
    HMENU snow = CreatePopupMenu();
    HMENU wind = CreatePopupMenu();

    std::wstring title = L"WinFlake";
    if (pauseReasons_ & kPauseBattery) title += L" — пауза (энергосбережение)";
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, title.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    std::wstring pauseText = (pauseReasons_ & kPauseUser) ? L"Продолжить" : L"Пауза";
    pauseText += hotkeyOk_ ? L"\tCtrl+Alt+S" : L"";
    AppendMenuW(menu, MF_STRING, kMenuPause, pauseText.c_str());

    AppendMenuW(snow, MF_STRING, kMenuLight, L"Лёгкий");
    AppendMenuW(snow, MF_STRING, kMenuSteady, L"Ровный");
    AppendMenuW(snow, MF_STRING, kMenuBlizzard, L"Метель");
    CheckMenuRadioItem(snow, kMenuLight, kMenuBlizzard, kMenuLight + static_cast<UINT>(settings_.intensity),
                       MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(snow), L"Снегопад");

    AppendMenuW(wind, MF_STRING, kMenuWindCalm, L"Штиль");
    AppendMenuW(wind, MF_STRING, kMenuWindLeft, L"Влево");
    AppendMenuW(wind, MF_STRING, kMenuWindRight, L"Вправо");
    AppendMenuW(wind, MF_SEPARATOR, 0, nullptr);
    const UINT strengthFlags = settings_.windDirection == sim::WindDirection::Calm ? MF_GRAYED : 0u;
    AppendMenuW(wind, MF_STRING | strengthFlags, kMenuWindWeak, L"Слабый");
    AppendMenuW(wind, MF_STRING | strengthFlags, kMenuWindMedium, L"Средний");
    AppendMenuW(wind, MF_STRING | strengthFlags, kMenuWindStrong, L"Сильный");
    CheckMenuRadioItem(wind, kMenuWindCalm, kMenuWindRight,
                       kMenuWindCalm + static_cast<UINT>(settings_.windDirection), MF_BYCOMMAND);
    CheckMenuRadioItem(wind, kMenuWindWeak, kMenuWindStrong,
                       kMenuWindWeak + static_cast<UINT>(settings_.windStrength - 1), MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(wind), L"Ветер");

    AppendMenuW(menu, MF_STRING | (settings_.wiper ? MF_CHECKED : 0u), kMenuWiper, L"Дворник");
    AppendMenuW(menu, MF_STRING, kMenuShovel, L"Лопата: убрать весь снег");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0u), kMenuAutostart,
                L"Запускать вместе с Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuHelp, L"Как пользоваться…");
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Выход");

    // Required so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(control_);
    const UINT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, at.x, at.y,
                                      control_, nullptr);
    PostMessageW(control_, WM_NULL, 0, 0);
    DestroyMenu(menu);  // also destroys the submenus

    bool settingsChanged = true;
    switch (cmd) {
        case kMenuPause:
            setPaused(kPauseUser, !(pauseReasons_ & kPauseUser));
            settingsChanged = false;
            break;
        case kMenuLight:
        case kMenuSteady:
        case kMenuBlizzard:
            settings_.intensity = static_cast<sim::Intensity>(cmd - kMenuLight);
            break;
        case kMenuWindCalm:
        case kMenuWindLeft:
        case kMenuWindRight:
            settings_.windDirection = static_cast<sim::WindDirection>(cmd - kMenuWindCalm);
            break;
        case kMenuWindWeak:
        case kMenuWindMedium:
        case kMenuWindStrong:
            settings_.windStrength = static_cast<int>(cmd - kMenuWindWeak) + 1;
            break;
        case kMenuShovel:
            world_.clear();
            framePending_ = true;
            settingsChanged = false;
            break;
        case kMenuWiper:
            settings_.wiper = !settings_.wiper;
            placeWiper();
            break;
        case kMenuAutostart:
            SetAutostart(!IsAutostartEnabled());
            settingsChanged = false;
            break;
        case kMenuHelp:
            showHelp();
            settingsChanged = false;
            break;
        case kMenuExit:
            DestroyWindow(control_);
            return;
        default:
            settingsChanged = false;
            break;
    }
    if (settingsChanged) {
        applySettings();
        SaveSettings(settings_);
    }
}

// ============================================================================
// Control window
// ============================================================================

LRESULT CALLBACK App::ControlProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->handleControl(hwnd, msg, wp, lp);
}

LRESULT App::handleControl(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_ != 0) {
        tray_.recreate();  // Explorer restarted
        return 0;
    }

    switch (msg) {
        case WM_APP_TRAY:
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU:
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    showTrayMenu({GET_X_LPARAM(wp), GET_Y_LPARAM(wp)});
                    break;
                default:
                    break;
            }
            return 0;

        case WM_HOTKEY:
            if (wp == kHotkeyPause) setPaused(kPauseUser, !(pauseReasons_ & kPauseUser));
            return 0;

        case WM_SETTINGCHANGE:
            if (wp != SPI_SETWORKAREA) break;  // taskbar moved / resized
            [[fallthrough]];
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            SetTimer(hwnd, kTimerDisplay, 400, nullptr);
            return 0;

        case WM_WTSSESSION_CHANGE:
            switch (wp) {
                case WTS_SESSION_LOCK:
                    setPaused(kPauseLocked, true);
                    break;
                case WTS_SESSION_UNLOCK:
                case WTS_CONSOLE_CONNECT:
                case WTS_REMOTE_CONNECT:
                    setPaused(kPauseLocked, false);
                    // The desktop switch may have cost us the D3D device.
                    SetTimer(hwnd, kTimerDisplay, 800, nullptr);
                    break;
                default:
                    break;
            }
            return 0;

        case WM_POWERBROADCAST:
            switch (wp) {
                case PBT_APMSUSPEND:
                    setPaused(kPauseSuspended, true);
                    break;
                case PBT_APMRESUMEAUTOMATIC:
                case PBT_APMRESUMESUSPEND:
                    setPaused(kPauseSuspended, false);
                    SetTimer(hwnd, kTimerDisplay, 1000, nullptr);
                    break;
                case PBT_APMPOWERSTATUSCHANGE:
                    checkAutoPause();
                    break;
                default:
                    break;
            }
            return TRUE;

        case WM_ENTERMENULOOP:
            SetTimer(hwnd, kTimerModalTick, 15, nullptr);
            return 0;
        case WM_EXITMENULOOP:
            KillTimer(hwnd, kTimerModalTick);
            return 0;

        case WM_TIMER:
            switch (wp) {
                case kTimerTopmost:
                    overlay_.reassertTopmost();
                    break;
                case kTimerDisplay:
                    KillTimer(hwnd, kTimerDisplay);
                    if (FAILED(rebuildDisplays())) onDeviceLost();
                    break;
                case kTimerRecover:
                    KillTimer(hwnd, kTimerRecover);
                    if (FAILED(rebuildDisplays())) SetTimer(hwnd, kTimerRecover, 2000, nullptr);
                    break;
                case kTimerModalTick:
                    tick();
                    break;
                case kTimerAutoPause:
                    checkAutoPause();
                    break;
                default:
                    break;
            }
            return 0;

        case WM_DESTROY:
            if (hotkeyOk_) UnregisterPauseHotkey(hwnd, kHotkeyPause);
            WTSUnRegisterSessionNotification(hwnd);
            tray_.destroy();
            control_ = nullptr;
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================================
// Entry
// ============================================================================

int App::run(HINSTANCE instance) {
    instance_ = instance;
    QueryPerformanceFrequency(&qpcFreq_);

    if (!createControlWindow()) return 1;
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

    settings_ = LoadSettings();
    applySettings();

    const UINT dpi = GetDpiForWindow(control_);
    trayIcon_ = CreateSnowflakeIcon(GetSystemMetricsForDpi(SM_CXSMICON, dpi));
    tray_.create(control_, WM_APP_TRAY, trayIcon_, L"WinFlake");

    hotkeyOk_ = RegisterPauseHotkey(control_, kHotkeyPause);
    WTSRegisterSessionNotification(control_, NOTIFY_FOR_THIS_SESSION);

    tickTimer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!tickTimer_) tickTimer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    if (!tickTimer_) return 1;

    tracker_.start();
    const HRESULT hr = rebuildDisplays();
    if (FAILED(hr)) {
        wchar_t text[200];
        swprintf_s(text, ARRAYSIZE(text),
                   L"Не удалось инициализировать Direct3D/DirectComposition.\nКод ошибки (HRESULT): 0x%08lX",
                   static_cast<unsigned long>(hr));
        MessageBoxW(nullptr, text, L"WinFlake", MB_ICONERROR | MB_OK);
        return 1;
    }
    SetTimer(control_, kTimerTopmost, 2000, nullptr);
    SetTimer(control_, kTimerAutoPause, 1000, nullptr);
    checkAutoPause();
    updateTooltip();

    QueryPerformanceCounter(&lastTick_);
    scheduleNextTick();

    for (;;) {
        const DWORD r = MsgWaitForMultipleObjectsEx(1, &tickTimer_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (r == WAIT_OBJECT_0) tick();

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}
