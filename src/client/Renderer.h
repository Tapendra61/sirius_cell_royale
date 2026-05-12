#pragma once

#include "Camera.h"
#include "Interpolator.h"
#include "core/Tuning.h"
#include "raylib.h"

namespace cr {

// Per-player palette. Exposed so the feel layer (particles, popups) can match cell colors.
inline Color colorForPlayer(PlayerId p) {
    static const Color palette[] = {
        Color{ 64, 156, 255, 255},
        Color{255, 120,  80, 255},
        Color{120, 220, 120, 255},
        Color{255, 200,  60, 255},
        Color{200, 120, 255, 255},
        Color{ 80, 220, 220, 255},
    };
    if (p == INVALID_PLAYER) return Color{180, 180, 180, 255};
    return palette[(p - 1) % (sizeof(palette) / sizeof(palette[0]))];
}

// Stateless renderer. Caller wraps drawWorld() in BeginMode2D/EndMode2D so it can
// share the camera with the feel layer (particles, popups) and apply screen shake.
class Renderer {
public:
    void drawWorld(const Interpolator&     interp,
                   const Tuning&           tuning,
                   float                   alpha,
                   EntityId                watched_cell = INVALID_ENTITY) const;
};

} // namespace cr
