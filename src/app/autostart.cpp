#include "app/autostart.h"

#include <windows.h>

#include <string>
#include <thread>

#include "app/package.h"

// C++/WinRT comes with the Windows SDK (MSVC builds). MinGW has neither it nor
// the StartupTask ABI headers: there the packaged mode reports Unavailable.
#if __has_include(<winrt/Windows.ApplicationModel.h>)
#define WINTERS_HAS_STARTUP_TASK 1
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#else
#define WINTERS_HAS_STARTUP_TASK 0
#endif

namespace {

// ---- portable: HKCU Run key ------------------------------------------------

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValue[] = L"WinTers";
constexpr wchar_t kOldValue[] = L"WinFlake";  // the app's former name

bool RunValueExists(const wchar_t* name) {
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) ==
           ERROR_SUCCESS;
}

bool DeleteRunValue(const wchar_t* name) {
    const LSTATUS st = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, name);
    return st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
}

bool SetRunValue(bool enabled) {
    if (!enabled) return DeleteRunValue(kValue);
    wchar_t exe[MAX_PATH * 2] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
    if (len == 0 || len >= ARRAYSIZE(exe)) return false;
    const std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
    return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValue, REG_SZ, cmd.c_str(),
                           static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

// ---- MSIX: StartupTask -----------------------------------------------------

#if WINTERS_HAS_STARTUP_TASK
namespace wam = winrt::Windows::ApplicationModel;

constexpr wchar_t kTaskId[] = L"WinTersStartup";  // = TaskId in packaging/AppxManifest.xml

AutostartState FromTaskState(wam::StartupTaskState s) {
    switch (s) {
        case wam::StartupTaskState::Disabled: return AutostartState::Disabled;
        case wam::StartupTaskState::Enabled: return AutostartState::Enabled;
        case wam::StartupTaskState::DisabledByUser: return AutostartState::DisabledByUser;
        case wam::StartupTaskState::DisabledByPolicy: return AutostartState::DisabledByPolicy;
        case wam::StartupTaskState::EnabledByPolicy: return AutostartState::EnabledByPolicy;
    }
    return AutostartState::Unavailable;
}

// The UI thread is an STA, and C++/WinRT must not block on an async operation
// there: run the (fast) call on a short-lived MTA thread and wait for it.
template <class F>
AutostartState OnMtaThread(F&& f) {
    AutostartState result = AutostartState::Unavailable;
    std::thread([&] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
            return;
        }
        try {
            result = f();
        } catch (...) {
            result = AutostartState::Unavailable;
        }
        winrt::uninit_apartment();
    }).join();
    return result;
}

AutostartState TaskState() {
    return OnMtaThread([] { return FromTaskState(wam::StartupTask::GetAsync(kTaskId).get().State()); });
}

bool SetTask(bool enabled) {
    const AutostartState s = OnMtaThread([enabled] {
        const wam::StartupTask task = wam::StartupTask::GetAsync(kTaskId).get();
        if (enabled) return FromTaskState(task.RequestEnableAsync().get());
        task.Disable();
        return FromTaskState(task.State());
    });
    return enabled ? (s == AutostartState::Enabled || s == AutostartState::EnabledByPolicy)
                   : (s == AutostartState::Disabled || s == AutostartState::DisabledByUser ||
                      s == AutostartState::DisabledByPolicy);
}
#else
AutostartState TaskState() { return AutostartState::Unavailable; }
bool SetTask(bool) { return false; }
#endif

}  // namespace

void MigrateAutostart() {
    if (IsPackaged()) {
        DeleteRunValue(kValue);
        DeleteRunValue(kOldValue);
        return;
    }
    if (!RunValueExists(kOldValue)) return;
    DeleteRunValue(kOldValue);
    SetRunValue(true);  // same choice, under the new name and this exe's path
}

AutostartState GetAutostartState() {
    if (IsPackaged()) return TaskState();
    return RunValueExists(kValue) ? AutostartState::Enabled : AutostartState::Disabled;
}

bool SetAutostart(bool enabled) { return IsPackaged() ? SetTask(enabled) : SetRunValue(enabled); }
