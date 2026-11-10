// "Start with Windows" via HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
#pragma once

bool IsAutostartEnabled();
bool SetAutostart(bool enabled);
