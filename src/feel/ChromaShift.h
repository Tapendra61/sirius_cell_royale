#pragma once

namespace cr {

// Cheap full-screen RGB channel offset (chromatic aberration) for "important moment"
// punctuation -- big absorbs and crits. The renderer composites the world layer three
// times (R, G, B) with a horizontal offset proportional to current intensity, so the
// bright pixels at edges of moving things fringe into red/blue.
//
// Triggers stack additively and decay exponentially with a ~100ms time constant, giving
// a quick pulse that's visible for ~300ms and then gone.
class ChromaShift {
public:
    // Bump effect intensity in [0, 1]. Stacking is additive, clamped at 1.
    void addShift(float intensity);

    // Exponential decay each frame.
    void update(float frame_dt);

    // Horizontal offset in pixels to apply per channel pass.
    float currentOffsetPx() const;

    // True when intensity is large enough that the 3-pass composite is worth doing.
    // When false, the renderer can take the cheap single-blit path.
    bool active() const { return intensity_ > 0.02f; }

private:
    float intensity_ = 0.0f;

    static constexpr float kMaxOffsetPx = 8.0f;
    static constexpr float kDecayTau    = 0.10f; // ~95% gone in 300ms
};

} // namespace cr
