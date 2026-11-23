// Conditions under which snow pauses by itself.
#pragma once

// Running on battery with Battery/Energy saver turned on.
bool IsBatterySaverActive();

// The foreground window belongs to another app and its client area covers its
// whole monitor: a full-screen game, video or presentation the user is in
// right now (not one left in the background). Deliberately does not use
// SHQueryUserNotificationState: that counted our own full-screen topmost
// overlay as a full-screen app, so the snow hid and reappeared in a loop.
bool IsFullscreenAppForeground();
