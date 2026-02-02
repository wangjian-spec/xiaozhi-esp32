#pragma once

#include <cstdint>

namespace app_ui {

class AnimationEngine {
public:
    bool Tick(uint32_t delta_ms);
    void StopAll();

private:
    bool dirty_ = false;
};

} // namespace app_ui
