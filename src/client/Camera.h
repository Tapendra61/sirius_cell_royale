#pragma once

#include "core/Types.h"
#include "raylib.h"

namespace cr {

// Smooth 2D follow camera with mass-driven zoom.
// zoom = clamp(base / sqrt(mass), min, max), lerped each frame toward the target.
class CameraController {
public:
    CameraController();

    void snapTo(Vec2 pos, float mass);
    void setTarget(Vec2 pos, float mass);
    void update(float frame_dt);

    Camera2D toCamera2D(int screen_w, int screen_h) const;
    Vec2     worldFromScreen(Vec2 screen_pos, int screen_w, int screen_h) const;

    Vec2  position() const { return pos_; }
    float zoom() const { return zoom_; }

private:
    Vec2  pos_{0.0f, 0.0f};
    float zoom_ = 1.0f;
    Vec2  target_pos_{0.0f, 0.0f};
    float target_mass_ = 100.0f;
};

} // namespace cr
