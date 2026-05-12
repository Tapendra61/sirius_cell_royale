#pragma once

#include "Camera.h"
#include "Interpolator.h"
#include "core/Tuning.h"

namespace cr {

// Stateless renderer. Owns no resources; reads the interpolator and the camera
// and draws into the current raylib draw call (caller wraps BeginDrawing/EndDrawing).
class Renderer {
public:
    void drawWorld(const Interpolator&     interp,
                   const CameraController& camera,
                   const Tuning&           tuning,
                   int                     screen_w,
                   int                     screen_h,
                   float                   alpha,
                   EntityId                watched_cell = INVALID_ENTITY) const;
};

} // namespace cr
