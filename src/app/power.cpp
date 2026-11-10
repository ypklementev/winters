#include "app/power.h"

#include <windows.h>

#include <shellapi.h>

bool IsFullscreenAppActive() {
    QUERY_USER_NOTIFICATION_STATE state{};
    if (FAILED(SHQueryUserNotificationState(&state))) return false;
    return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN ||
           state == QUNS_PRESENTATION_MODE;
}

bool IsBatterySaverActive() {
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps)) return false;
    // SystemStatusFlag: 1 = battery saver on (Windows 10+). ACLineStatus 0 = on battery.
    return ps.SystemStatusFlag == 1 && ps.ACLineStatus == 0;
}
