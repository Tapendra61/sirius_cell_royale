#include "ChromaShift.h"

#include <algorithm>
#include <cmath>

namespace cr {

void ChromaShift::addShift(float i) {
    intensity_ = std::min(1.0f, intensity_ + std::max(0.0f, i));
}

void ChromaShift::update(float dt) {
    if (intensity_ <= 0.0f || dt <= 0.0f) return;
    intensity_ *= std::exp(-dt / kDecayTau);
    if (intensity_ < 0.001f) intensity_ = 0.0f;
}

float ChromaShift::currentOffsetPx() const {
    return intensity_ * kMaxOffsetPx;
}

} // namespace cr
