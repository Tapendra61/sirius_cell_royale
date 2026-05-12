#include "Hitstop.h"

#include <algorithm>

namespace cr {

void Hitstop::trigger(float duration_sec) {
    remaining_ = std::max(remaining_, duration_sec);
}

void Hitstop::update(float frame_dt) {
    remaining_ = std::max(0.0f, remaining_ - frame_dt);
}

} // namespace cr
