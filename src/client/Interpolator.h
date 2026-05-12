#pragma once

#include "core/Snapshot.h"

namespace cr {

// Holds the latest two snapshots so the renderer can mix them by alpha
// (= leftover-time-in-accumulator / sim_dt). Render frequency is decoupled
// from sim frequency this way.
class Interpolator {
public:
    void push(Snapshot s);

    bool hasCurr() const { return has_curr_; }
    bool hasPrev() const { return has_prev_; }

    const Snapshot& curr() const { return curr_; }
    const Snapshot& prev() const { return prev_; }

    // Convenience: interpolated position for a given cell id. Returns {0,0}
    // if the cell isn't in the current snapshot.
    Vec2 cellPos(EntityId id, float alpha) const;

private:
    Snapshot prev_;
    Snapshot curr_;
    bool     has_prev_ = false;
    bool     has_curr_ = false;
};

} // namespace cr
