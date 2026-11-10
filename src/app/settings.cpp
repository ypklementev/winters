#include "app/settings.h"

#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

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

int ReadInt(const std::wstring& path, const wchar_t* key, int def) {
    return static_cast<int>(GetPrivateProfileIntW(kSection, key, def, path.c_str()));
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
    s.autoPauseFullscreen = ReadInt(path, L"AutoPauseFullscreen", s.autoPauseFullscreen ? 1 : 0) != 0;
    return sim::sanitize(s);
}

void SaveSettings(const sim::UserSettings& s) {
    const std::wstring path = SettingsPath(true);
    if (path.empty()) return;
    WriteInt(path, L"Intensity", static_cast<int>(s.intensity));
    WriteInt(path, L"WindDirection", static_cast<int>(s.windDirection));
    WriteInt(path, L"WindStrength", s.windStrength);
    WriteInt(path, L"AutoPauseFullscreen", s.autoPauseFullscreen ? 1 : 0);
}
