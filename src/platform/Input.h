#pragma once

#include "core/Types.h"
#include "raylib.h"

namespace cr {

// Per-frame input snapshot. Move target is continuous; the four Pressed flags are
// edge-triggered (true only on the frame the action was triggered).
struct InputState {
    Vec2 worldMoveTarget{};
    bool moveActive   = true;
    bool splitPressed = false;
    bool ejectPressed = false;
    bool dashPressed  = false;
    bool blastPressed = false; // 4th ability: mass-spend radial shockwave
    bool pausePressed = false;
};

// Player-configurable input behavior. Touch flags are no-ops on desktop and vice-versa.
struct InputConfig {
    bool hold_to_move  = false; // desktop: only emit move while left mouse held
    bool invert_thumbs = false; // touch: swap which side has joystick vs buttons
};

// Top-level entrypoint. Dispatches to desktop or touch based on the runtime flag
// (default is platform-dependent; setForceTouch() overrides).
InputState pollInput(const Camera2D& cam,
                     int screen_w, int screen_h,
                     const InputConfig& cfg);

InputState pollInputDesktop(const Camera2D& cam,
                            int screen_w, int screen_h,
                            const InputConfig& cfg);
InputState pollInputTouch(const Camera2D& cam,
                          int screen_w, int screen_h,
                          const InputConfig& cfg);

// Draw the floating joystick + on-screen buttons. No-op when not using the touch path.
void renderTouchOverlay(int screen_w, int screen_h, const InputConfig& cfg);

// Touch / desktop selection. Defaults to touch on Android, desktop elsewhere; can be
// overridden at runtime (used by the dev console `force_touch` command for testing).
void setForceTouch(bool force);
bool isUsingTouch();

} // namespace cr
