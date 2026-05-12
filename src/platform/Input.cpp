#include "Input.h"

namespace cr {

namespace {

#if defined(PLATFORM_ANDROID) || defined(__ANDROID__) || defined(PLATFORM_IOS)
bool gUseTouch = true;
#else
bool gUseTouch = false;
#endif

} // namespace

void setForceTouch(bool force) { gUseTouch = force; }
bool isUsingTouch() { return gUseTouch; }

InputState pollInput(const Camera2D& cam, int sw, int sh, const InputConfig& cfg) {
    if (gUseTouch) return pollInputTouch(cam, sw, sh, cfg);
    return pollInputDesktop(cam, sw, sh, cfg);
}

} // namespace cr
