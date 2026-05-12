#pragma once

namespace cr {

// On big absorbs, freeze the simulation for ~30-80ms while continuing to render. The
// reason this works: the player's brain reads the pause as the impact "registering",
// and big eats feel meaty. trigger() overlaps (takes the longer remaining time).
class Hitstop {
public:
    void  trigger(float duration_sec);
    void  update(float frame_dt);
    bool  isFrozen() const { return remaining_ > 0.0f; }
    float remaining() const { return remaining_; }

private:
    float remaining_ = 0.0f;
};

} // namespace cr
