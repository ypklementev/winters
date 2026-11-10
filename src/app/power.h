// Conditions under which snow pauses by itself.
#pragma once

// A full-screen app/game/presentation is in the foreground.
bool IsFullscreenAppActive();
// Running on battery with Battery/Energy saver turned on.
bool IsBatterySaverActive();
