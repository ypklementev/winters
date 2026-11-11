// macOS persistence (NSUserDefaults, i.e. ~/Library/Preferences/<bundle id>.plist)
// and "Open at Login" (SMAppService, macOS 13+).
#pragma once

#include "sim/settings_model.h"

// wiperX/wiperY are global display coordinates in points on macOS.
sim::UserSettings LoadSettings();
void SaveSettings(const sim::UserSettings& s);

enum class LoginItemState {
    Disabled,
    Enabled,
    RequiresApproval,  // registered, but switched off in System Settings > General > Login Items
    Unavailable,
};

LoginItemState GetLoginItemState();
bool SetLoginItem(bool enabled);
void OpenLoginItemsSettings();
