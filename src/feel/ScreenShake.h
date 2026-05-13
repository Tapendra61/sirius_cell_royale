#pragma once

#include "core/Types.h"

namespace cr {

// Trauma-based screen shake (Squirrel Eiserloh, GDC 2013). addTrauma() pushes the value
// up to 1.0; each frame it decays. The actual offset uses trauma squared so small bumps
// stay small and big bumps shake hard.
class ScreenShake {
public:
    void  addTrauma(float t);
    void  update(float frame_dt);
    Vec2  sampleOffset(float max_offset) const;
    float trauma() const { return trauma_; }

    // Accessibility setting -- user-tunable multiplier applied to the sampled offset.
    // 0 = no shake at all, 1 = roadmap default, up to 1.5 for shake-fans.
    void  setScale(float s);
    float scale() const { return scale_; }

private:
    float trauma_ = 0.0f;
    float time_   = 0.0f;
    float scale_  = 1.0f;
};

} // namespace cr
