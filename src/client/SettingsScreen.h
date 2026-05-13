#pragma once

#include "meta/SaveFile.h"
#include "raylib.h"

namespace cr {

// What the settings screen wants the outer loop to do after this frame.
enum class SettingsAction {
    None,
    BackToMenu,
    Quit,
};

// Phase 9 accessibility / preferences screen. Edits a SaveData reference in place;
// changes flush to disk when the player eventually exits or returns to menu (the
// outer loop in runWindow handles persistence). Audio volumes are applied live so
// the player can hear the change while dragging; FPS cap is applied on "back".
//
// Doesn't own any state of its own except a scroll position (currently unused; the
// settings list is short enough to fit on a 1280x720 screen).
class SettingsScreen {
public:
    SettingsAction render(int screen_w, int screen_h, SaveData& save);
};

} // namespace cr
