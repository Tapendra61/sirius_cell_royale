#include "Input.h"

namespace cr {

InputState pollInputDesktop(const Camera2D& cam, int /*sw*/, int /*sh*/,
                            const InputConfig& cfg) {
    InputState s;

    Vector2 mp    = GetMousePosition();
    Vector2 world = GetScreenToWorld2D(mp, cam);
    s.worldMoveTarget = {world.x, world.y};

    if (cfg.hold_to_move) {
        s.moveActive = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    } else {
        s.moveActive = true;
    }

    s.splitPressed = IsKeyPressed(KEY_SPACE);
    s.ejectPressed = IsKeyPressed(KEY_W);
    s.dashPressed  = IsKeyPressed(KEY_LEFT_SHIFT)
                  || IsKeyPressed(KEY_RIGHT_SHIFT)
                  || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    s.pausePressed = IsKeyPressed(KEY_ESCAPE);

    return s;
}

} // namespace cr
