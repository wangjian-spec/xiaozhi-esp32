#pragma once

#include <cstddef>
#include <cstdint>

namespace app_ui {

struct Size {
    int16_t w = 0;
    int16_t h = 0;

    bool operator==(const Size& other) const {
        return w == other.w && h == other.h;
    }

    bool operator!=(const Size& other) const {
        return !(*this == other);
    }
};

struct Point {
    int16_t x = 0;
    int16_t y = 0;
};

struct Rect {
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;

    bool IsEmpty() const {
        return w <= 0 || h <= 0;
    }

    bool Contains(Point p) const {
        return p.x >= x && p.y >= y && p.x < (x + w) && p.y < (y + h);
    }

    bool Intersects(const Rect& other) const {
        return !(x + w <= other.x || other.x + other.w <= x ||
                 y + h <= other.y || other.y + other.h <= y);
    }

    Rect Union(const Rect& other) const {
        if (IsEmpty()) {
            return other;
        }
        if (other.IsEmpty()) {
            return *this;
        }
        const int16_t x1 = (x < other.x) ? x : other.x;
        const int16_t y1 = (y < other.y) ? y : other.y;
        const int16_t x2 = ((x + w) > (other.x + other.w)) ? (x + w) : (other.x + other.w);
        const int16_t y2 = ((y + h) > (other.y + other.h)) ? (y + h) : (other.y + other.h);
        return {x1, y1, static_cast<int16_t>(x2 - x1), static_cast<int16_t>(y2 - y1)};
    }

    Rect Offset(Point p) const {
        return {static_cast<int16_t>(x + p.x), static_cast<int16_t>(y + p.y), w, h};
    }
};

enum class WidgetType : uint8_t {
    Label,
    Image,
    Progress,
    TextArea,
    Button,
    Checkbox,
    Switch,
    Radio,
    ListView,
    TabView,
    Frame,
    Menu,
    Dialog,
    SoftKeyboard,
    TopBar,
    BottomBar,
    Unknown = 0xFF,
};

enum class SceneID : uint16_t {
    Invalid = 0xFFFF,
};

enum class PropertyKey : uint8_t {
    Text,
    Image,
    Font,
    Style,
    Value,
    Custom = 0xFF,
};

enum class Anchor : uint8_t {
    LT,
    RT,
    LB,
    RB,
    Center,
    Top,
    Bottom,
    Left,
    Right,
};

enum class Gravity : uint8_t {
    None,
    Center,
    Left,
    Right,
    Top,
    Bottom,
};

constexpr uint16_t kInvalidWidgetIndex = 0xFFFF;
constexpr uint16_t kInvalidLayoutId = 0xFFFF;
constexpr uint16_t kInvalidStyleId = 0xFFFF;

struct GeneratedMetaDisplay {
    const char* type;
    int width;
    int height;
    const char* color;
};

struct GeneratedMetaGenerator {
    const char* tool;
    const char* version;
};

struct GeneratedMeta {
    const char* project;
    const char* ui_version;
    const char* target;
    GeneratedMetaDisplay display;
    GeneratedMetaGenerator generator;
};

struct GeneratedTextResource {
    uint32_t id;
    const char* text;
};

struct GeneratedImageResource {
    uint32_t id;
    const char* file;
    int width;
    int height;
};

struct GeneratedFontResource {
    uint32_t id;
    const char* file;
    int height;
};

struct GeneratedStyleProperty {
    const char* key;
    const char* value;
};

struct GeneratedStyle {
    const char* id;
    const GeneratedStyleProperty* props;
    size_t prop_count;
};

struct GeneratedThemeMapping {
    const char* style_key;
    const char* style_value;
};

struct GeneratedTheme {
    const char* id;
    const GeneratedThemeMapping* mappings;
    size_t mapping_count;
};

struct GeneratedDataEntry {
    const char* category;
    const char* id;
    const char* type;
    const char* json;
};

struct PropertyDesc {
    PropertyKey key;
    uint32_t value;
};

struct LayoutDesc {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    Anchor anchor;
    Gravity gravity;
};

struct WidgetDesc {
    uint32_t id;
    WidgetType type;
    uint16_t layout_id;
    uint16_t style_id;
    uint16_t prop_start;
    uint8_t prop_count;
    uint16_t parent;
    uint16_t flags;
};

struct SceneDesc {
    SceneID id;
    uint16_t widget_start;
    uint16_t widget_count;
    uint16_t flags;
};

struct LayoutPackage {
    const GeneratedMeta* meta;
    const SceneDesc* scenes;
    size_t scene_count;
    const WidgetDesc* widgets;
    size_t widget_count;
    const LayoutDesc* layouts;
    size_t layout_count;
    const PropertyDesc* properties;
    size_t property_count;
    const uint16_t* scene_widget_indices;
    size_t scene_widget_index_count;
};

namespace resource {

struct WidgetInit {
    uint32_t id;
    uint32_t parent_id; // 0 = root
    WidgetType type;
    Rect rect;
    uint32_t style_id;
    uint32_t data_id;
};

struct SceneInit {
    uint32_t scene_id;
    uint32_t root_widget_id;
};

struct UIResource {
    const GeneratedMeta* meta;
    const GeneratedTextResource* texts;
    size_t text_count;
    const GeneratedImageResource* images;
    size_t image_count;
    const GeneratedFontResource* fonts;
    size_t font_count;
    const GeneratedStyle* styles;
    size_t style_count;
    const GeneratedDataEntry* data_entries;
    size_t data_entry_count;
    const WidgetInit* widgets;
    size_t widget_count;
    const SceneInit* scenes;
    size_t scene_count;
};

} // namespace resource

struct GeneratedEventBinding {
    uint32_t widget_id;
    const char* event_type;
    void (*handler)();
    const char* handler_name;
};

struct GeneratedFocusEntry {
    const char* widget;
    const char* up;
    const char* down;
    const char* left;
    const char* right;
};

struct GeneratedSceneFlowEntry {
    const char* scene;
    const char* on_back;
};

struct GeneratedStateEntry {
    const char* widget;
    const char* state;
    const char* json;
};

} // namespace app_ui
