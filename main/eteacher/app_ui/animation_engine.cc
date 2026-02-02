#include "animation_engine.h"

namespace app_ui {

bool AnimationEngine::Tick(uint32_t) {
    if (dirty_) {
        dirty_ = false;
        return true;
    }
    return false;
}

void AnimationEngine::StopAll() {
    dirty_ = false;
}

} // namespace app_ui
