#pragma once

#include "raylib.h"

namespace cr {

// Sub-menu reached from MainMenu's ROYALE button. Picks between Local (LAN multiplayer
// hosted from this machine) and Global (matchmaking-backed online play). Global is a
// placeholder for now; selecting it shows an inline "coming soon" toast and stays put.
enum class RoyaleMenuAction {
    None,
    ShowLocalLobby,    // navigate to the LocalLobby screen (Host / Join)
    BackToMainMenu,    // user clicked BACK or hit ESC
    Quit,              // user closed the OS window via shortcut (rare from this screen)
};

class RoyaleMenu {
public:
    void             update(float frame_dt, int screen_w, int screen_h);
    RoyaleMenuAction render(int screen_w, int screen_h);

private:
    // Visual heartbeat shared with the main menu's background drift. RoyaleMenu doesn't
    // animate cells (kept simple), but the title still pulses.
    float anim_time_              = 0.0f;
    // Set to a positive value when the user clicks GLOBAL -- counts down each frame and
    // drives the "coming soon" toast.
    float global_toast_remaining_ = 0.0f;
};

} // namespace cr
