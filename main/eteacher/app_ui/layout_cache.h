#pragma once

#include <cstdint>
#include "geometry.h"

namespace app_ui {

struct LayoutCache {
    Size measured{};
    Rect layout{};
    bool measure_valid = false;
    bool layout_valid = false;
    uint32_t measure_version = 0;
    uint32_t layout_version = 0;
    Size last_constraint{};
};

} // namespace app_ui
