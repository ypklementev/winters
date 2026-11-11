#include "app/settings.h"

#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

#include <cwchar>
#include <string>

namespace {

constexpr wchar_t kSection[] = L"WinFlake";

std::wstring SettingsPath(bool createDir) {
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) return {};
    std::wstring dir = std::wstring(appData) + L"\\WinFlake";
    CoTaskMemFree(appData);
    if (createDir) CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

// GetPrivateProfileIntW clamps negatives to 0, but positions left of / above
// the primary monitor are negative: parse the string ourselves.
int ReadInt(const std::wstring& path, const wchar_t* key, int def) {
    wchar_t buf[32] = {};
    GetPrivateProfileStringW(kSection, key, L"", buf, ARRAYSIZE(buf), path.c_str());
    if (buf[0] == L'\0') return def;
    wchar_t* end = nullptr;
    const long v = std::wcstol(buf, &end, 10);
    return end == buf ? def : static_cast<int>(v);
}

void WriteInt(const std::wstring& path, const wchar_t* key, int v) {
    WritePrivateProfileStringW(kSection, key, std::to_wstring(v).c_str(), path.c_str());
}

}  // namespace

sim::UserSettings LoadSettings() {
    sim::UserSettings s;
    const std::wstring path = SettingsPath(false);
    if (path.empty()) return s;
    s.intensity = static_cast<sim::Intensity>(ReadInt(path, L"Intensity", static_cast<int>(s.intensity)));
    s.windDirection =
        static_cast<sim::WindDirection>(ReadInt(path, L"WindDirection", static_cast<int>(s.windDirection)));
    s.windStrength = ReadInt(path, L"WindStrength", s.windStrength);
    s.wiper = ReadInt(path, L"Wiper", 0) != 0;
    s.wiperPlaced = ReadInt(path, L"WiperPlaced", 0) != 0;
    s.wiperX = ReadInt(path, L"WiperX", 0);
    s.wiperY = ReadInt(path, L"WiperY", 0);
    s.unlimited = ReadInt(path, L"Unlimited", 0) != 0;
    return sim::sanitize(s);
}

void SaveSettings(const sim::UserSettings& s) {
    const std::wstring path = SettingsPath(true);
    if (path.empty()) return;
    WriteInt(path, L"Intensity", static_cast<int>(s.intensity));
    WriteInt(path, L"WindDirection", static_cast<int>(s.windDirection));
    WriteInt(path, L"WindStrength", s.windStrength);
    WriteInt(path, L"Wiper", s.wiper ? 1 : 0);
    WriteInt(path, L"WiperPlaced", s.wiperPlaced ? 1 : 0);
    WriteInt(path, L"WiperX", s.wiperX);
    WriteInt(path, L"WiperY", s.wiperY);
    WriteInt(path, L"Unlimited", s.unlimited ? 1 : 0);
}
