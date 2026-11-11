#include "app/power.h"

#include <windows.h>

bool IsBatterySaverActive() {
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps)) return false;
    // SystemStatusFlag: 1 = battery saver on (Windows 10+). ACLineStatus 0 = on battery.
    return ps.SystemStatusFlag == 1 && ps.ACLineStatus == 0;
}
