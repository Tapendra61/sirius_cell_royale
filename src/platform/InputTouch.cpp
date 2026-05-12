#include "Input.h"

namespace cr {

namespace {

struct Rect { float x, y, w, h; };

struct TouchJoystick {
    bool  active = false;
    Vec2  anchor{};
    Vec2  current{};
    float deadzone   = 4.0f;
    float max_radius = 96.0f;
};

struct ButtonState {
    bool down         = false;
    bool just_pressed = false;
};

TouchJoystick gJoystick;
ButtonState   gSplit, gEject, gDash, gPause;

constexpr float kButtonSize = 72.0f;
constexpr float kButtonPad  = 16.0f;
constexpr float kPauseSize  = 56.0f;

Rect actionBtnRect(int sw, int sh, int slot, const InputConfig& cfg) {
    // slot 0 = bottommost (Split), 1 = Eject, 2 = Dash
    float x = cfg.invert_thumbs ? kButtonPad
                                : (static_cast<float>(sw) - kButtonSize - kButtonPad);
    float y = static_cast<float>(sh) - kButtonPad
            - kButtonSize - static_cast<float>(slot) * (kButtonSize + 8.0f);
    return {x, y, kButtonSize, kButtonSize};
}

Rect pauseBtnRect(int sw, int /*sh*/, const InputConfig& cfg) {
    float x = cfg.invert_thumbs ? kButtonPad
                                : (static_cast<float>(sw) - kPauseSize - kButtonPad);
    return {x, kButtonPad, kPauseSize, kPauseSize};
}

bool insideRect(Vector2 p, Rect r) {
    return p.x >= r.x && p.x <= r.x + r.w
        && p.y >= r.y && p.y <= r.y + r.h;
}

bool inJoystickHalf(Vector2 t, int sw, const InputConfig& cfg) {
    bool left = t.x < static_cast<float>(sw) * 0.5f;
    return cfg.invert_thumbs ? !left : left;
}

void updateButton(ButtonState& b, Rect r, int touch_count) {
    bool now_down = false;
    for (int i = 0; i < touch_count; ++i) {
        if (insideRect(GetTouchPosition(i), r)) { now_down = true; break; }
    }
    b.just_pressed = now_down && !b.down;
    b.down         = now_down;
}

} // namespace

InputState pollInputTouch(const Camera2D& cam, int sw, int sh, const InputConfig& cfg) {
    InputState state;
    state.moveActive = false;

    const int n = GetTouchPointCount();

    const Rect r_split = actionBtnRect(sw, sh, 0, cfg);
    const Rect r_eject = actionBtnRect(sw, sh, 1, cfg);
    const Rect r_dash  = actionBtnRect(sw, sh, 2, cfg);
    const Rect r_pause = pauseBtnRect(sw, sh, cfg);

    // Find a joystick touch: first finger in the joystick half that's not on a button.
    bool    joystick_touch = false;
    Vector2 j_pos{};
    for (int i = 0; i < n; ++i) {
        Vector2 t = GetTouchPosition(i);
        if (!inJoystickHalf(t, sw, cfg)) continue;
        if (insideRect(t, r_split) || insideRect(t, r_eject)
            || insideRect(t, r_dash) || insideRect(t, r_pause)) continue;
        joystick_touch = true;
        j_pos          = t;
        break;
    }

    if (joystick_touch) {
        if (!gJoystick.active) {
            gJoystick.active = true;
            gJoystick.anchor = {j_pos.x, j_pos.y};
        }
        gJoystick.current = {j_pos.x, j_pos.y};

        Vec2  delta = gJoystick.current - gJoystick.anchor;
        float mag   = length(delta);
        if (mag > gJoystick.deadzone) {
            Vec2 dir = delta * (1.0f / mag);
            // Aim at a point ~500 world units ahead of the camera centre.
            // The cell will continuously chase, which is the steering behaviour we want.
            state.worldMoveTarget = {cam.target.x + dir.x * 500.0f,
                                     cam.target.y + dir.y * 500.0f};
            state.moveActive      = true;
        }
    } else {
        gJoystick.active = false;
    }

    updateButton(gSplit, r_split, n);
    updateButton(gEject, r_eject, n);
    updateButton(gDash,  r_dash,  n);
    updateButton(gPause, r_pause, n);

    state.splitPressed = gSplit.just_pressed;
    state.ejectPressed = gEject.just_pressed;
    state.dashPressed  = gDash.just_pressed;
    state.pausePressed = gPause.just_pressed;

    return state;
}

void renderTouchOverlay(int sw, int sh, const InputConfig& cfg) {
    if (!isUsingTouch()) return;

    if (gJoystick.active) {
        DrawCircleLines(static_cast<int>(gJoystick.anchor.x),
                        static_cast<int>(gJoystick.anchor.y),
                        gJoystick.max_radius,
                        Color{255, 255, 255, 100});
        // Clamp knob to anchor + max_radius.
        Vec2  delta = gJoystick.current - gJoystick.anchor;
        float mag   = length(delta);
        Vec2  knob  = gJoystick.current;
        if (mag > gJoystick.max_radius) {
            Vec2 dir = delta * (1.0f / mag);
            knob = gJoystick.anchor + dir * gJoystick.max_radius;
        }
        DrawCircleV(Vector2{knob.x, knob.y}, 28.0f, Color{220, 220, 220, 200});
        DrawCircleLines(static_cast<int>(knob.x), static_cast<int>(knob.y), 28.0f,
                        Color{40, 40, 40, 220});
    }

    auto drawBtn = [](Rect r, const ButtonState& bs, const char* label) {
        Color fill = bs.down ? Color{120, 160, 220, 220} : Color{60, 60, 70, 180};
        DrawRectangleRounded(Rectangle{r.x, r.y, r.w, r.h}, 0.3f, 8, fill);
        int font_size = 18;
        int tw        = MeasureText(label, font_size);
        DrawText(label,
                 static_cast<int>(r.x + (r.w - tw) * 0.5f),
                 static_cast<int>(r.y + (r.h - font_size) * 0.5f),
                 font_size, RAYWHITE);
    };

    drawBtn(actionBtnRect(sw, sh, 0, cfg), gSplit, "Split");
    drawBtn(actionBtnRect(sw, sh, 1, cfg), gEject, "Eject");
    drawBtn(actionBtnRect(sw, sh, 2, cfg), gDash,  "Dash");
    drawBtn(pauseBtnRect(sw, sh, cfg),     gPause, "II");
}

} // namespace cr
