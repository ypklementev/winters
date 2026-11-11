// "Start with Windows" via HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
#pragma once

bool IsAutostartEnabled();
bool SetAutostart(bool enabled);
// Moves a "Start with Windows" entry left by the old WinFlake name over to
// WinTers (pointing at the running exe).
void MigrateAutostart();
