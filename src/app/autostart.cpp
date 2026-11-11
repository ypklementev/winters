#include "app/autostart.h"

#include <windows.h>

#include <string>

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValue[] = L"WinTers";
constexpr wchar_t kOldValue[] = L"WinFlake";  // the app's former name
}  // namespace

void MigrateAutostart() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kOldValue, RRF_RT_REG_SZ, nullptr, nullptr, &size) !=
        ERROR_SUCCESS) {
        return;
    }
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kOldValue);
    SetAutostart(true);  // same choice, under the new name and this exe's path
}

bool IsAutostartEnabled() {
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValue, RRF_RT_REG_SZ, nullptr, nullptr, &size) ==
           ERROR_SUCCESS;
}

bool SetAutostart(bool enabled) {
    if (!enabled) {
        const LSTATUS st = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValue);
        return st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
    }
    wchar_t exe[MAX_PATH * 2] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
    if (len == 0 || len >= ARRAYSIZE(exe)) return false;
    const std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
    return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValue, REG_SZ, cmd.c_str(),
                           static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}
