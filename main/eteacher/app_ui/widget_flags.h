#pragma once

#include <cstdint>

namespace app_ui {

struct WidgetFlags {
    uint8_t visible : 1;
    uint8_t enabled : 1;
    uint8_t focusable : 1;
    uint8_t focused : 1;
    uint8_t dirty : 1;
    uint8_t layout_dirty : 1;

    WidgetFlags()
        : visible(1),
          enabled(1),
          focusable(0),
          focused(0),
          dirty(1),
          layout_dirty(1) {}
};

} // namespace app_ui
