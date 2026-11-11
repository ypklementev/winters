#include "app/settings.h"

#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

#include <cwchar>
#include <string>

namespace {

constexpr wchar_t kSection[] = L"WinTers";
// The app used to be called WinFlake: its settings are picked up once.
constexpr wchar_t kOldName[] = L"WinFlake";

std::wstring SettingsPath(const wchar_t* name, bool createDir) {
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) return {};
    std::wstring dir = std::wstring(appData) + L"\\" + name;
    CoTaskMemFree(appData);
    if (createDir) CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

bool FileExists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// GetPrivateProfileIntW clamps negatives to 0, but positions left of / above
// the primary monitor are negative: parse the string ourselves.
int ReadInt(const std::wstring& path, const wchar_t* key, int def, const wchar_t* section = kSection) {
    wchar_t buf[32] = {};
    GetPrivateProfileStringW(section, key, L"", buf, ARRAYSIZE(buf), path.c_str());
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
    std::wstring path = SettingsPath(kSection, false);
    if (path.empty()) return s;
    const wchar_t* section = kSection;
    bool migrate = false;
    if (!FileExists(path)) {
        const std::wstring old = SettingsPath(kOldName, false);
        if (!old.empty() && FileExists(old)) {
            path = old;
            section = kOldName;
            migrate = true;
        }
    }
    auto read = [&](const wchar_t* key, int def) { return ReadInt(path, key, def, section); };
    s.intensity = static_cast<sim::Intensity>(read(L"Intensity", static_cast<int>(s.intensity)));
    s.windDirection = static_cast<sim::WindDirection>(read(L"WindDirection", static_cast<int>(s.windDirection)));
    s.windStrength = read(L"WindStrength", s.windStrength);
    s.wiper = read(L"Wiper", 0) != 0;
    s.wiperPlaced = read(L"WiperPlaced", 0) != 0;
    s.wiperX = read(L"WiperX", 0);
    s.wiperY = read(L"WiperY", 0);
    s.unlimited = read(L"Unlimited", 0) != 0;
    s = sim::sanitize(s);
    if (migrate) SaveSettings(s);
    return s;
}

void SaveSettings(const sim::UserSettings& s) {
    const std::wstring path = SettingsPath(kSection, true);
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
