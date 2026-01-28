#include "eteacher/app_ui/layout_engine.h"

namespace eteacher::app_ui::layout {

namespace {

int16_t ClampNonNegative(int16_t v) { return v < 0 ? 0 : v; }

struct WorkingArea {
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;
};

WorkingArea MakeContentArea(const ScreenInfo &screen, const LayoutParams &params)
{
    WorkingArea area;
    area.x = params.margin.left;
    area.y = params.margin.top;
    area.w = static_cast<int16_t>(screen.width - params.margin.left - params.margin.right);
    area.h = static_cast<int16_t>(screen.height - params.margin.top - params.margin.bottom);
    area.w = ClampNonNegative(area.w);
    area.h = ClampNonNegative(area.h);
    return area;
}

void ApplyFocus(const LayoutParams &params, LayoutResult &out)
{
    if (!params.enable_focus || out.count == 0) {
        return;
    }
    if (params.custom_focus) {
        if (auto *r = out.FindMutable(params.focus_id)) {
            r->focused = true;
        }
        return;
    }
    for (std::size_t i = 0; i < out.count; ++i) {
        if (out.regions[i].id.type == RegionType::Primary) {
            out.regions[i].focused = true;
            return;
        }
    }
    for (std::size_t i = 0; i < out.count; ++i) {
        if (out.regions[i].id.type == RegionType::List) {
            out.regions[i].focused = true;
            return;
        }
    }
}

int16_t ResolveHeight(const SizeSpec &spec, int16_t base, int16_t auto_value)
{
    return ClampNonNegative(spec.Resolve(base, auto_value));
}

void DistributeAutoHeights(int16_t remaining, uint8_t auto_count, int16_t *out_each, uint8_t *out_remainder)
{
    if (!out_each || !out_remainder || auto_count == 0) {
        return;
    }
    if (remaining < 0) {
        remaining = 0;
    }
    *out_each = static_cast<int16_t>(remaining / auto_count);
    *out_remainder = static_cast<uint8_t>(remaining % auto_count);
}

void ApplyCommonRegionFlags(const LayoutParams &params, Region *region)
{
    if (!region) {
        return;
    }
    region->partial_refresh = params.partial_refresh_default;
}

void BuildOverlayRegions(const ScreenInfo &screen, const LayoutParams &params, LayoutResult &out, bool freeze_underlay)
{
    if (params.overlay_count == 0) {
        return;
    }
    auto area = MakeContentArea(screen, params);
    const int16_t overlay_w = ResolveHeight(params.overlay_width, area.w, area.w);
    const int16_t overlay_h = ResolveHeight(params.overlay_height, area.h, area.h);

    const int16_t clamped_w = overlay_w > area.w ? area.w : overlay_w;
    const int16_t clamped_h = overlay_h > area.h ? area.h : overlay_h;

    const int16_t base_x = static_cast<int16_t>(area.x + (area.w - clamped_w) / 2);
    const int16_t base_y = static_cast<int16_t>(area.y + (area.h - clamped_h) / 2);

    for (uint8_t i = 0; i < params.overlay_count; ++i) {
        Rect rect{base_x, base_y, clamped_w, clamped_h};
        if (auto *r = out.AddRegion(RegionType::Overlay, i, rect)) {
            r->z_order = static_cast<int8_t>(100 + i);
            r->partial_refresh = params.partial_refresh_default;
        }
    }

    if (freeze_underlay) {
        for (std::size_t i = 0; i < out.count; ++i) {
            if (out.regions[i].id.type != RegionType::Overlay) {
                out.regions[i].frozen = true;
            }
        }
    }
}

void BuildHeaderFooter(const LayoutParams &params,
                       const WorkingArea &area,
                       int16_t header_h,
                       int16_t footer_h,
                       LayoutResult &out)
{
    if (header_h > 0) {
        Rect rect{0, 0, area.w, header_h};
        if (auto *r = out.AddRegion(RegionType::Header, 0, rect)) {
            ApplyCommonRegionFlags(params, r);
        }
    }
    if (footer_h > 0) {
        Rect rect{0, static_cast<int16_t>(area.h - footer_h), area.w, footer_h};
        if (auto *r = out.AddRegion(RegionType::Footer, 0, rect)) {
            ApplyCommonRegionFlags(params, r);
        }
    }
}

void BuildVerticalRegionGroup(RegionType type,
                              uint8_t count,
                              int16_t start_y,
                              int16_t gap,
                              int16_t width,
                              int16_t each_height,
                              uint8_t remainder,
                              const LayoutParams &params,
                              LayoutResult &out)
{
    int16_t y = start_y;
    for (uint8_t i = 0; i < count; ++i) {
        int16_t h = each_height;
        if (remainder > 0) {
            h = static_cast<int16_t>(h + 1);
            remainder--;
        }
        Rect rect{0, 0, 0, 0};
        rect.x = 0;
        rect.y = y;
        rect.w = width;
        rect.h = h;
        if (auto *r = out.AddRegion(type, i, rect)) {
            ApplyCommonRegionFlags(params, r);
        }
        y = static_cast<int16_t>(y + h + gap);
    }
}

void OffsetRegions(LayoutResult &out, int16_t dx, int16_t dy)
{
    for (std::size_t i = 0; i < out.count; ++i) {
        out.regions[i].rect.x = static_cast<int16_t>(out.regions[i].rect.x + dx);
        out.regions[i].rect.y = static_cast<int16_t>(out.regions[i].rect.y + dy);
    }
}

int16_t ComputeGroupHeight(uint8_t count, int16_t each_height, uint8_t remainder, int16_t gap)
{
    if (count == 0) {
        return 0;
    }
    int16_t height = static_cast<int16_t>(each_height * count);
    if (remainder > count) {
        remainder = count;
    }
    height = static_cast<int16_t>(height + remainder);
    height = static_cast<int16_t>(height + gap * count);
    return height;
}

void BuildFocusContentLayout(const ScreenInfo &screen, const LayoutParams &params, LayoutResult &out)
{
    const auto area = MakeContentArea(screen, params);
    WorkingArea local{0, 0, area.w, area.h};
    const uint8_t primary_count = params.primary_count == 0 ? 1 : params.primary_count;
    const uint8_t secondary_count = params.secondary_count;

    const uint8_t header_count = 1;
    const uint8_t footer_count = 1;

    const uint8_t block_count = static_cast<uint8_t>(header_count + footer_count + primary_count + secondary_count);
    const int16_t total_gap = static_cast<int16_t>(params.gap * (block_count > 0 ? (block_count - 1) : 0));

    const int16_t header_h = ResolveHeight(params.header_height, area.h, 0);
    const int16_t footer_h = ResolveHeight(params.footer_height, area.h, 0);

    int16_t fixed_primary_h = 0;
    int16_t fixed_secondary_h = 0;
    uint8_t auto_primary = 0;
    uint8_t auto_secondary = 0;

    if (params.primary_height.unit == SizeUnit::Auto) {
        auto_primary = primary_count;
    } else {
        fixed_primary_h = static_cast<int16_t>(primary_count * ResolveHeight(params.primary_height, area.h, 0));
    }

    if (params.secondary_height.unit == SizeUnit::Auto) {
        auto_secondary = secondary_count;
    } else {
        fixed_secondary_h = static_cast<int16_t>(secondary_count * ResolveHeight(params.secondary_height, area.h, 0));
    }

    int16_t remaining = static_cast<int16_t>(area.h - header_h - footer_h - total_gap - fixed_primary_h - fixed_secondary_h);
    remaining = ClampNonNegative(remaining);

    const uint8_t auto_count = static_cast<uint8_t>(auto_primary + auto_secondary);
    int16_t auto_each = 0;
    uint8_t auto_remainder = 0;
    DistributeAutoHeights(remaining, auto_count, &auto_each, &auto_remainder);

    int16_t y_cursor = 0;
    BuildHeaderFooter(params, local, header_h, footer_h, out);

    y_cursor = static_cast<int16_t>(header_h + (header_h > 0 ? params.gap : 0));

    int16_t primary_each = (params.primary_height.unit == SizeUnit::Auto)
                               ? auto_each
                               : ResolveHeight(params.primary_height, area.h, 0);
    uint8_t primary_rem = 0;
    if (params.primary_height.unit == SizeUnit::Auto) {
        primary_rem = auto_remainder > primary_count ? primary_count : auto_remainder;
    }

    BuildVerticalRegionGroup(RegionType::Primary,
                             primary_count,
                             y_cursor,
                             params.gap,
                             local.w,
                             primary_each,
                             primary_rem,
                             params,
                             out);

    y_cursor = static_cast<int16_t>(y_cursor + ComputeGroupHeight(primary_count, primary_each, primary_rem, params.gap));

    int16_t secondary_each = (params.secondary_height.unit == SizeUnit::Auto)
                                 ? auto_each
                                 : ResolveHeight(params.secondary_height, area.h, 0);
    uint8_t secondary_rem = 0;
    if (params.secondary_height.unit == SizeUnit::Auto) {
        if (auto_remainder > primary_rem) {
            secondary_rem = static_cast<uint8_t>(auto_remainder - primary_rem);
            if (secondary_rem > secondary_count) {
                secondary_rem = secondary_count;
            }
        }
    }

    BuildVerticalRegionGroup(RegionType::Secondary,
                             secondary_count,
                             y_cursor,
                             params.gap,
                             local.w,
                             secondary_each,
                             secondary_rem,
                             params,
                             out);

    OffsetRegions(out, area.x, area.y);
}

void BuildListBrowseLayout(const ScreenInfo &screen, const LayoutParams &params, LayoutResult &out)
{
    const auto area = MakeContentArea(screen, params);
    WorkingArea local{0, 0, area.w, area.h};
    const uint8_t list_count = params.list_count == 0 ? 1 : params.list_count;
    const uint8_t block_count = static_cast<uint8_t>(list_count + 2); // header + footer + list(s)

    const int16_t total_gap = static_cast<int16_t>(params.gap * (block_count > 0 ? (block_count - 1) : 0));
    const int16_t header_h = ResolveHeight(params.header_height, area.h, 0);
    const int16_t footer_h = ResolveHeight(params.footer_height, area.h, 0);

    int16_t fixed_list_h = 0;
    uint8_t auto_list = 0;
    if (params.list_height.unit == SizeUnit::Auto) {
        auto_list = list_count;
    } else {
        fixed_list_h = static_cast<int16_t>(list_count * ResolveHeight(params.list_height, area.h, 0));
    }

    int16_t remaining = static_cast<int16_t>(area.h - header_h - footer_h - total_gap - fixed_list_h);
    remaining = ClampNonNegative(remaining);

    int16_t auto_each = 0;
    uint8_t auto_remainder = 0;
    DistributeAutoHeights(remaining, auto_list, &auto_each, &auto_remainder);

    BuildHeaderFooter(params, local, header_h, footer_h, out);

    int16_t y_cursor = static_cast<int16_t>(header_h + (header_h > 0 ? params.gap : 0));

    int16_t list_each = (params.list_height.unit == SizeUnit::Auto)
                            ? auto_each
                            : ResolveHeight(params.list_height, area.h, 0);
    uint8_t list_rem = (params.list_height.unit == SizeUnit::Auto) ? auto_remainder : 0;

    BuildVerticalRegionGroup(RegionType::List,
                             list_count,
                             y_cursor,
                             params.gap,
                             local.w,
                             list_each,
                             list_rem,
                             params,
                             out);

    OffsetRegions(out, area.x, area.y);
}

void BuildFullScreenPrimary(const ScreenInfo &screen, const LayoutParams &params, LayoutResult &out)
{
    const auto area = MakeContentArea(screen, params);
    Rect rect{area.x, area.y, area.w, area.h};
    if (auto *r = out.AddRegion(RegionType::Primary, 0, rect)) {
        ApplyCommonRegionFlags(params, r);
    }
}

} // namespace

LayoutResult LayoutEngine::Compute(const LayoutTemplate &layout,
                                   const ScreenInfo &screen,
                                   const LayoutParams &params)
{
    LayoutResult result;
    result.Clear();

    if (layout.custom_builder) {
        if (layout.custom_builder(screen, params, result)) {
            ApplyFocus(params, result);
            return result;
        }
    }

    switch (layout.kind) {
    case LayoutTemplate::Kind::ListBrowse:
        BuildListBrowseLayout(screen, params, result);
        break;
    case LayoutTemplate::Kind::Dialog:
        BuildFocusContentLayout(screen, params, result);
        BuildOverlayRegions(screen, params, result, true);
        break;
    case LayoutTemplate::Kind::FullScreenText:
        BuildFullScreenPrimary(screen, params, result);
        break;
    case LayoutTemplate::Kind::InputKeyboard:
        BuildFocusContentLayout(screen, params, result);
        break;
    case LayoutTemplate::Kind::FocusContent:
    default:
        BuildFocusContentLayout(screen, params, result);
        break;
    }

    const bool freeze_underlay = params.freeze_under_overlay || layout.kind == LayoutTemplate::Kind::Dialog;
    BuildOverlayRegions(screen, params, result, freeze_underlay);

    ApplyFocus(params, result);
    return result;
}

} // namespace eteacher::app_ui::layout
