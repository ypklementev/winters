#pragma once

#include <windows.h>

// Procedurally drawn snowflake (white with a blue outline, readable on both
// light and dark taskbars). Caller owns the icon (DestroyIcon).
HICON CreateSnowflakeIcon(int size);
