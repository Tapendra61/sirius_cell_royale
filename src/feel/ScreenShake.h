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

private:
    float trauma_ = 0.0f;
    float time_   = 0.0f;
};

} // namespace cr
