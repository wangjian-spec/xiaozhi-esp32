#pragma once

#include <cstddef>
#include <cstdint>

#include "types.h"

namespace app_ui::desc {

constexpr uint16_t kWidgetFlagVisible = 1u << 0;
constexpr uint16_t kWidgetFlagEnabled = 1u << 1;
constexpr uint16_t kWidgetFlagFocusable = 1u << 2;

struct TextDesc {
    const char* text;
    uint32_t text_id;
};

struct CheckableDesc {
    const char* text;
    uint32_t text_id;
    bool checked;
};

struct ProgressDesc {
    const char* text;
    uint32_t text_id;
    uint8_t value;
};

struct WidgetDesc {
    uint32_t id;
    uint32_t parent_id;
    WidgetType type;
    Rect rect;
    uint32_t style_id;
    uint16_t flags;
    int16_t z_order;
    const void* specific;
};

struct SceneDesc {
    const char* id;
    uint32_t root_id;
    const WidgetDesc* widgets;
    size_t widget_count;
};

struct UiDesc {
    const SceneDesc* scenes;
    size_t scene_count;
    const SceneDesc* public_scene;
};

} // namespace app_ui::desc
