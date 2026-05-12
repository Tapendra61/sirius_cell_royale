#include "ScreenShake.h"

#include <algorithm>
#include <cmath>

namespace cr {

void ScreenShake::addTrauma(float t) {
    trauma_ = std::min(1.0f, trauma_ + t);
}

void ScreenShake::update(float frame_dt) {
    trauma_ = std::max(0.0f, trauma_ - frame_dt * 1.5f);
    time_  += frame_dt;
}

Vec2 ScreenShake::sampleOffset(float max_offset) const {
    if (trauma_ <= 0.0f) return {0.0f, 0.0f};
    const float shake = trauma_ * trauma_;
    // Pseudo-perlin: a couple of incommensurate sinusoids give a Perlin-ish wobble cheaply.
    const float t = time_;
    const float nx = std::sin(t * 41.3f) * 0.6f + std::sin(t * 17.7f) * 0.4f;
    const float ny = std::cos(t * 37.9f) * 0.6f + std::cos(t * 23.1f) * 0.4f;
    return {nx * max_offset * shake, ny * max_offset * shake};
}

} // namespace cr
