// "Start with Windows".
//   portable exe: HKCU\Software\Microsoft\Windows\CurrentVersion\Run
//   MSIX:         Windows.ApplicationModel.StartupTask "WinTersStartup"
#pragma once

enum class AutostartState {
    Disabled,
    Enabled,
    DisabledByUser,    // MSIX: turned off in Settings > Apps > Startup; only the user can turn it back on there
    DisabledByPolicy,  // MSIX: blocked by group policy
    EnabledByPolicy,   // MSIX: forced on by group policy
    Unavailable,       // MSIX: StartupTask failed or this build has no C++/WinRT (MinGW)
};

AutostartState GetAutostartState();
bool SetAutostart(bool enabled);
// Run once at startup.
//   portable: moves a Run entry left by the old WinFlake name over to WinTers.
//   MSIX: removes Run entries left by the portable exe so Windows does not
//         start the app twice (the StartupTask is the only autostart then).
void MigrateAutostart();
