#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 轻量布局引擎：专为墨水屏 MCU 设计（内存占用小，结构清晰）。
// 说明：仅负责计算区域 Rect 与状态，不涉及具体绘制。

namespace eteacher::layout {

// 屏幕矩形。
struct Rect {
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;

    bool IsEmpty() const { return w <= 0 || h <= 0; }
    int16_t right() const { return static_cast<int16_t>(x + w); }
    int16_t bottom() const { return static_cast<int16_t>(y + h); }
};

// 屏幕边距。
struct EdgeInsets {
    int16_t left = 0;
    int16_t top = 0;
    int16_t right = 0;
    int16_t bottom = 0;
};

enum class Orientation : uint8_t {
    Auto = 0,
    Portrait,
    Landscape,
};

// 屏幕信息。
struct ScreenInfo {
    int16_t width = 0;
    int16_t height = 0;
    Orientation orientation = Orientation::Auto;

    bool IsLandscape() const {
        if (orientation == Orientation::Landscape) {
            return true;
        }
        if (orientation == Orientation::Portrait) {
            return false;
        }
        return width >= height;
    }
};

// 分区类型。
enum class RegionType : uint8_t {
    Header,
    Primary,
    Secondary,
    List,
    Footer,
    Overlay,
};

// 分区 ID：可扩展多个 Primary/Secondary/List/Overlay。
struct RegionId {
    RegionType type = RegionType::Primary;
    uint8_t index = 0;

    bool operator==(const RegionId &other) const { return type == other.type && index == other.index; }
    bool operator!=(const RegionId &other) const { return !(*this == other); }
};

// 分区信息。
struct Region {
    RegionId id{};          // 分区唯一 ID
    Rect rect{};            // 分区矩形
    bool visible = true;    // 是否可见
    bool focused = false;   // 是否焦点
    bool partial_refresh = true; // 是否局部刷新
    bool frozen = false;    // 是否冻结（Dialog/Overlay 时常用）
    int8_t z_order = 0;     // 绘制层级（越大越靠上）
};

// 尺寸单位。
enum class SizeUnit : uint8_t {
    Auto = 0,
    Px,
    Percent,
};

// 尺寸描述：支持自动/像素/百分比。
struct SizeSpec {
    SizeUnit unit = SizeUnit::Auto;
    int16_t value = 0;

    static SizeSpec Auto() { return {SizeUnit::Auto, 0}; }
    static SizeSpec Px(int16_t px) { return {SizeUnit::Px, px}; }
    static SizeSpec Percent(uint8_t percent) { return {SizeUnit::Percent, static_cast<int16_t>(percent)}; }

    // 根据基准长度解析尺寸；auto_value 为 Auto 时的默认值。
    int16_t Resolve(int16_t base, int16_t auto_value) const {
        switch (unit) {
        case SizeUnit::Px:
            return value;
        case SizeUnit::Percent:
            return static_cast<int16_t>((base * value) / 100);
        case SizeUnit::Auto:
        default:
            return auto_value;
        }
    }
};

// 布局参数（可按模板或屏幕调整）。
struct LayoutParams {
    EdgeInsets margin{6, 6, 6, 6};  // 屏幕边距
    int16_t gap = 4;               // 区域之间的间距

    // 默认尺寸（可被百分比/像素覆盖）
    SizeSpec header_height = SizeSpec::Px(24);
    SizeSpec footer_height = SizeSpec::Px(24);
    SizeSpec primary_height = SizeSpec::Auto();
    SizeSpec secondary_height = SizeSpec::Auto();
    SizeSpec list_height = SizeSpec::Auto();

    // Overlay 默认尺寸（居中显示）
    SizeSpec overlay_width = SizeSpec::Percent(70);
    SizeSpec overlay_height = SizeSpec::Percent(40);

    uint8_t primary_count = 1;
    uint8_t secondary_count = 1;
    uint8_t list_count = 1;
    uint8_t overlay_count = 0;

    bool partial_refresh_default = true; // 默认局部刷新开关
    bool freeze_under_overlay = false;   // Overlay 是否冻结底层

    // 默认焦点
    bool enable_focus = true;
    bool custom_focus = false;
    RegionId focus_id{}; // custom_focus=true 时生效
};

// 最大分区数量（小内存设计）。
constexpr std::size_t kMaxRegions = 24;

// 布局结果：按顺序存放分区。
struct LayoutResult {
    std::array<Region, kMaxRegions> regions{};
    uint8_t count = 0;

    void Clear() { count = 0; }

    Region* AddRegion(const Region &region) {
        if (count >= kMaxRegions) {
            return nullptr;
        }
        regions[count] = region;
        return &regions[count++];
    }

    Region* AddRegion(RegionType type, uint8_t index, const Rect &rect) {
        Region region;
        region.id = {type, index};
        region.rect = rect;
        return AddRegion(region);
    }

    Region* FindMutable(RegionId id) {
        for (std::size_t i = 0; i < count; ++i) {
            if (regions[i].id == id) {
                return &regions[i];
            }
        }
        return nullptr;
    }

    const Region* Find(RegionId id) const {
        for (std::size_t i = 0; i < count; ++i) {
            if (regions[i].id == id) {
                return &regions[i];
            }
        }
        return nullptr;
    }
};

// 自定义模板构建函数（返回 true 表示构建成功）。
typedef bool (*LayoutBuildFn)(const ScreenInfo &screen, const LayoutParams &params, LayoutResult &out);

// 布局模板：内置模板 + 可扩展自定义模板。
struct LayoutTemplate {
    enum class Kind : uint8_t {
        FocusContent,
        ListBrowse,
        Dialog,
        FullScreenText,
        InputKeyboard,
        Custom,
    };

    Kind kind = Kind::FocusContent;
    const char *name = "FocusContent";
    LayoutBuildFn custom_builder = nullptr;

    static LayoutTemplate FocusContent() { return {Kind::FocusContent, "FocusContent", nullptr}; }
    static LayoutTemplate ListBrowse() { return {Kind::ListBrowse, "ListBrowse", nullptr}; }
    static LayoutTemplate Dialog() { return {Kind::Dialog, "Dialog", nullptr}; }
    static LayoutTemplate FullScreenText() { return {Kind::FullScreenText, "FullScreenText", nullptr}; }
    static LayoutTemplate InputKeyboard() { return {Kind::InputKeyboard, "InputKeyboard", nullptr}; }
    static LayoutTemplate Custom(const char *n, LayoutBuildFn fn) { return {Kind::Custom, n ? n : "Custom", fn}; }
};

// 布局引擎：根据模板 + 屏幕信息 + 参数计算 LayoutResult。
class LayoutEngine {
public:
    static LayoutResult Compute(const LayoutTemplate &layout,
                                const ScreenInfo &screen,
                                const LayoutParams &params);
};

} // namespace eteacher::layout
