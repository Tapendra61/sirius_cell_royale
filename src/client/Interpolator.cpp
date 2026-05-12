#include "Interpolator.h"

#include <utility>

namespace cr {

void Interpolator::push(Snapshot s) {
    if (has_curr_) {
        prev_     = std::move(curr_);
        has_prev_ = true;
    }
    curr_     = std::move(s);
    has_curr_ = true;
}

Vec2 Interpolator::cellPos(EntityId id, float alpha) const {
    if (!has_curr_) return {0.0f, 0.0f};
    Vec2  curr_pos{0.0f, 0.0f};
    bool  found = false;
    for (const auto& c : curr_.cells) {
        if (c.id == id) {
            curr_pos = c.pos;
            found    = true;
            break;
        }
    }
    if (!found) return {0.0f, 0.0f};
    if (!has_prev_) return curr_pos;
    for (const auto& c : prev_.cells) {
        if (c.id == id) {
            return lerp(c.pos, curr_pos, alpha);
        }
    }
    return curr_pos;
}

} // namespace cr
