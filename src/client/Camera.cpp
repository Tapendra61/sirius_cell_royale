#include "Camera.h"

#include <algorithm>
#include <cmath>

namespace cr {

namespace {

constexpr float kBaseZoom = 10.0f;
constexpr float kMinZoom  = 0.3f;
constexpr float kMaxZoom  = 1.5f;
constexpr float kLerpRate = 8.0f; // larger = snappier; chosen for "responsive but not jittery"

float computeZoom(float mass) {
    float m = std::max(1.0f, mass);
    return std::clamp(kBaseZoom / std::sqrt(m), kMinZoom, kMaxZoom);
}

} // namespace

CameraController::CameraController() = default;

void CameraController::snapTo(Vec2 pos, float mass) {
    pos_         = pos;
    zoom_        = computeZoom(mass);
    target_pos_  = pos;
    target_mass_ = mass;
}

void CameraController::setTarget(Vec2 pos, float mass) {
    target_pos_  = pos;
    target_mass_ = mass;
}

void CameraController::update(float frame_dt) {
    float t = 1.0f - std::exp(-kLerpRate * frame_dt);
    pos_    = lerp(pos_, target_pos_, t);
    zoom_   = lerp(zoom_, computeZoom(target_mass_), t);
}

Camera2D CameraController::toCamera2D(int screen_w, int screen_h) const {
    Camera2D c{};
    c.target.x = pos_.x;
    c.target.y = pos_.y;
    c.offset.x = static_cast<float>(screen_w) * 0.5f;
    c.offset.y = static_cast<float>(screen_h) * 0.5f;
    c.rotation = 0.0f;
    c.zoom     = zoom_;
    return c;
}

Vec2 CameraController::worldFromScreen(Vec2 screen_pos, int screen_w, int screen_h) const {
    Camera2D c   = toCamera2D(screen_w, screen_h);
    Vector2  rl  = {screen_pos.x, screen_pos.y};
    Vector2  out = GetScreenToWorld2D(rl, c);
    return {out.x, out.y};
}

} // namespace cr
