// %APPDATA%\WinFlake\settings.ini persistence.
#pragma once

#include "sim/settings_model.h"

sim::UserSettings LoadSettings();
void SaveSettings(const sim::UserSettings& s);
