#include "eteacher/app_manager/menu.h"

#include <algorithm>
#include <Adafruit_GFX.h>
#include <array>
#include <cmath>
#include <cstring>
#include "assets.h"
#include <esp_log.h>
#include <vector>
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/font_manager/font_manager.h"

namespace eteacher::app_menu {
namespace {

static constexpr char kTag[] = "Menu";

struct BinImage {
    const uint8_t* data = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    size_t data_size = 0;
};

static inline uint16_t ReadLE16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

bool LoadBinImage(const std::string& name, BinImage* out) {
    if (!out) {
        return false;
    }
    void* ptr = nullptr;
    size_t size = 0;
    if (!Assets::GetInstance().GetAssetData(name, ptr, size) || !ptr || size < 4) {
        ESP_LOGW(kTag, "Icon not found or too small: %s", name.c_str());
        return false;
    }
    const auto* data = static_cast<const uint8_t*>(ptr);
    const uint16_t w = ReadLE16(data);
    const uint16_t h = ReadLE16(data + 2);
    const size_t stride = (w + 7u) / 8u;
    const size_t bytes = static_cast<size_t>(stride) * h;
    if (size < 4 + bytes) {
        ESP_LOGW(kTag, "Icon size mismatch: %s (w=%u h=%u size=%u)", name.c_str(), w, h, (unsigned)size);
        static std::vector<uint8_t> scratch;
        if (scratch.size() < bytes) {
            scratch.resize(bytes);
        }
        std::fill(scratch.begin(), scratch.begin() + bytes, 0x00);
        const size_t available = size > 4 ? (size - 4) : 0;
        if (available > 0) {
            std::memcpy(scratch.data(), data + 4, std::min(available, bytes));
        }
        out->data = scratch.data();
        out->width = w;
        out->height = h;
        out->data_size = bytes;
        return true;
    }

    out->data = data + 4;
    out->width = w;
    out->height = h;
    out->data_size = bytes;
    return true;
}

int16_t GetFontHeight(std::string_view font_name) {
    const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
    if (!font || !font->Ready()) {
        return 12;
    }
    const auto& header = font->Header();
    return static_cast<int16_t>(header.ascent + header.descent);
}

int16_t GetFontAscent(std::string_view font_name) {
    const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
    if (!font || !font->Ready()) {
        return 9;
    }
    return static_cast<int16_t>(font->Header().ascent);
}

void DrawIconWithText(Adafruit_GFX& gfx,
                      CustomEpdDisplay* epd,
                      const std::string& icon_name,
                      std::string_view text,
                      int16_t right_x,
                      int16_t top_y,
                      int16_t bar_h,
                      int16_t baseline_y,
                      std::string_view font_name,
                      int16_t gap,
                      int16_t* out_left_x) {
    if (!epd) {
        if (out_left_x) {
            *out_left_x = right_x;
        }
        return;
    }

    int16_t text_width = epd->MeasureUtf8Width(text, font_name);
    BinImage icon;
    bool has_icon = LoadBinImage(icon_name, &icon);
    int16_t icon_w = has_icon ? static_cast<int16_t>(icon.width) : 0;
    int16_t icon_h = has_icon ? static_cast<int16_t>(icon.height) : 0;

    int16_t total_w = text_width + (has_icon ? (gap + icon_w) : 0);
    int16_t left_x = static_cast<int16_t>(right_x - total_w);

    int16_t cursor = left_x;
    if (has_icon && icon.data && icon_w > 0 && icon_h > 0) {
        const int16_t icon_y = static_cast<int16_t>(top_y + (bar_h - icon_h) / 2);
        gfx.drawBitmap(cursor, icon_y, icon.data, icon_w, icon_h, GxEPD_BLACK);
        cursor = static_cast<int16_t>(cursor + icon_w + gap);
    }

    if (!text.empty()) {
        epd->DrawUtf8(cursor, baseline_y, text, font_name, GxEPD_BLACK);
    }

    if (out_left_x) {
        *out_left_x = left_x;
    }
}

void DrawSelectionRect(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t border) {
    if (w <= 0 || h <= 0 || border <= 0) {
        return;
    }
    int16_t radius = static_cast<int16_t>(std::min<int16_t>(8, std::min<int16_t>(w, h) / 4));
    for (int i = 0; i < border; ++i) {
        gfx.drawRoundRect(x - i, y - i, w + i * 2, h + i * 2, radius + i, GxEPD_BLACK);
    }
}

void DrawBatteryIcon(Adafruit_GFX& gfx, int16_t right_x, int16_t top_y, int16_t bar_h, int level) {
    const int16_t battery_w = 22;
    const int16_t battery_h = 12;
    const int16_t nub_w = 3;
    const int16_t nub_h = std::max<int16_t>(2, static_cast<int16_t>(battery_h / 2));
    const int16_t radius = 2;

    const int16_t x = static_cast<int16_t>(right_x - (battery_w + nub_w));
    const int16_t y = static_cast<int16_t>(top_y + (bar_h - battery_h) / 2);

    gfx.drawRoundRect(x, y, battery_w, battery_h, radius, GxEPD_BLACK);
    const int16_t nub_y = static_cast<int16_t>(y + (battery_h - nub_h) / 2);
    gfx.fillRoundRect(static_cast<int16_t>(x + battery_w - 1), nub_y, nub_w, nub_h, radius, GxEPD_BLACK);

    if (level < 0) {
        return;
    }
    const int scaled2 = level * 2;
    int blocks = 0;
    if (scaled2 < 25) {
        blocks = 0;
    } else if (scaled2 < 75) {
        blocks = 1;
    } else if (scaled2 < 125) {
        blocks = 2;
    } else if (scaled2 < 175) {
        blocks = 3;
    } else {
        blocks = 4;
    }
    const int16_t padding = 1;
    const int16_t inner_x = static_cast<int16_t>(x + padding);
    const int16_t inner_y = static_cast<int16_t>(y + padding);
    const int16_t inner_w = static_cast<int16_t>(battery_w - padding * 2);
    const int16_t inner_h = static_cast<int16_t>(battery_h - padding * 2);
    const int16_t gap = 1;
    // clear inner area first to avoid leftover single-pixel columns
    gfx.fillRect(inner_x, inner_y, inner_w, inner_h, GxEPD_WHITE);
    const int16_t block_w = static_cast<int16_t>((inner_w - gap * 3) / 4);
    if (block_w <= 0 || inner_h <= 0) {
        return;
    }
    for (int i = 0; i < blocks; ++i) {
        const int16_t bx = static_cast<int16_t>(inner_x + i * (block_w + gap));
        gfx.fillRect(bx, inner_y, block_w, inner_h, GxEPD_BLACK);
    }
}

void DrawFivePointStar(Adafruit_GFX& gfx,
                       int16_t center_x,
                       int16_t center_y,
                       int16_t outer_r,
                       int16_t inner_r,
                       uint16_t color) {
    if (outer_r <= 0 || inner_r <= 0) {
        return;
    }
    struct Point {
        int16_t x;
        int16_t y;
    };
    std::array<Point, 10> pts{};
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kDegToRad = kPi / 180.0f;
    for (int i = 0; i < 5; ++i) {
        const float outer_angle = (-90.0f + i * 72.0f) * kDegToRad;
        const float inner_angle = (-90.0f + i * 72.0f + 36.0f) * kDegToRad;
        pts[i * 2] = {
            static_cast<int16_t>(std::lround(center_x + outer_r * std::cos(outer_angle))),
            static_cast<int16_t>(std::lround(center_y + outer_r * std::sin(outer_angle)))
        };
        pts[i * 2 + 1] = {
            static_cast<int16_t>(std::lround(center_x + inner_r * std::cos(inner_angle))),
            static_cast<int16_t>(std::lround(center_y + inner_r * std::sin(inner_angle)))
        };
    }
    for (int i = 0; i < 10; ++i) {
        const auto& p0 = pts[i];
        const auto& p1 = pts[(i + 1) % 10];
        gfx.drawLine(p0.x, p0.y, p1.x, p1.y, color);
    }
}

} // namespace

MenuLayout Menu::ComputeLayout(int16_t screen_w, int16_t screen_h, size_t /*item_count*/) const {
    MenuLayout layout;
    layout.area_x = style_.padding;
    layout.area_y = style_.top_height;
    layout.area_w = static_cast<int16_t>(screen_w - style_.padding * 2);
    layout.area_h = static_cast<int16_t>(screen_h - style_.top_height - style_.bottom_height);

    const int16_t label_height = GetFontHeight(style_.label_font);
    const int16_t cell_w = style_.icon_cell_w;
    const int16_t cell_h = static_cast<int16_t>(style_.icon_cell_h + style_.icon_label_gap + label_height);

    int16_t columns = 1;
    int16_t rows = 1;
    if (cell_w > 0) {
        columns = std::max<int16_t>(1, static_cast<int16_t>((layout.area_w + style_.col_gap) / (cell_w + style_.col_gap)));
    }
    if (cell_h > 0) {
        rows = std::max<int16_t>(1, static_cast<int16_t>((layout.area_h + style_.row_gap) / (cell_h + style_.row_gap)));
    }

    layout.columns = columns;
    layout.rows = rows;
    layout.items_per_page = static_cast<int16_t>(std::max<int>(1, columns * rows));
    layout.cell_w = cell_w;
    layout.cell_h = cell_h;
    layout.label_height = label_height;

    return layout;
}

void Menu::Draw(Adafruit_GFX& gfx,
                CustomEpdDisplay* epd,
                const std::vector<MenuItem>& items,
                int selected_index,
                const MenuStatus& status,
                std::string_view footer_text) const {
    if (!epd) {
        return;
    }

    const int16_t screen_w = static_cast<int16_t>(epd->width());
    const int16_t screen_h = static_cast<int16_t>(epd->height());

    // Outer frame 400x300
    const int16_t frame_w = 400;
    const int16_t frame_h = 300;
    const int16_t frame_x = static_cast<int16_t>((screen_w - frame_w) / 2);
    const int16_t frame_y = static_cast<int16_t>((screen_h - frame_h) / 2);
    gfx.drawRect(frame_x, frame_y, frame_w, frame_h, GxEPD_BLACK);

    const auto layout = ComputeLayout(screen_w, screen_h, items.size());

    // Top bar
    const int16_t status_font_ascent = GetFontAscent(style_.status_font);
    const int16_t status_font_height = GetFontHeight(style_.status_font);
    const int16_t status_baseline = static_cast<int16_t>((style_.top_height - status_font_height) / 2 + status_font_ascent);

    const int16_t vol_batt_gap = 50; // spacing between volume and battery
    const int16_t batt_wifi_gap = 35; // spacing between battery and wifi
    const int16_t status_right = static_cast<int16_t>(screen_w - style_.padding);
    const int16_t volume_group_right = status_right; // rightmost anchor for volume group
    const int16_t battery_right = static_cast<int16_t>(status_right - vol_batt_gap);
    const int16_t wifi_right = static_cast<int16_t>(battery_right - batt_wifi_gap);

    // Draw volume icon to the LEFT of the numeric text
    BinImage volume_icon;
    bool has_volume = LoadBinImage("volume.bin", &volume_icon);
    if (has_volume && volume_icon.data && volume_icon.width > 0 && volume_icon.height > 0) {
        const int16_t icon_w = static_cast<int16_t>(volume_icon.width);
        const int16_t icon_h = static_cast<int16_t>(volume_icon.height);
        const int16_t icon_text_gap = 4;
        // Fixed area width for volume text: 30px
        const int16_t fixed_text_w = 20;
        const int16_t text_w = status.volume_text.empty() ? 0 : fixed_text_w;
        const int16_t total_w = icon_w + (text_w > 0 ? (icon_text_gap + text_w) : 0);
        const int16_t icon_x = static_cast<int16_t>(volume_group_right - total_w);
        const int16_t icon_y = static_cast<int16_t>((style_.top_height - icon_h) / 2);
        gfx.drawBitmap(icon_x, icon_y, volume_icon.data, icon_w, icon_h, GxEPD_BLACK);
        if (!status.volume_text.empty()) {
            const int16_t area_x = static_cast<int16_t>(icon_x + icon_w + icon_text_gap);
            const int16_t area_right = static_cast<int16_t>(area_x + text_w);
            const int16_t measured_w = epd->MeasureUtf8Width(status.volume_text, style_.status_font);
            const int16_t draw_x = static_cast<int16_t>(std::max<int16_t>(area_x, area_right - measured_w));
            epd->DrawUtf8(draw_x, status_baseline, status.volume_text, style_.status_font, GxEPD_BLACK);
        }
    } else {
        // fallback: no icon available, draw volume text in fixed 30px area right-aligned
        const int16_t fixed_text_w = 30;
        const int16_t text_w = status.volume_text.empty() ? 0 : fixed_text_w;
        if (text_w > 0) {
            const int16_t area_x = static_cast<int16_t>(volume_group_right - text_w);
            const int16_t area_right = static_cast<int16_t>(volume_group_right);
            const int16_t measured_w = epd->MeasureUtf8Width(status.volume_text, style_.status_font);
            const int16_t draw_x = static_cast<int16_t>(std::max<int16_t>(area_x, area_right - measured_w));
            epd->DrawUtf8(draw_x, status_baseline, status.volume_text, style_.status_font, GxEPD_BLACK);
        }
    }

    DrawBatteryIcon(gfx, battery_right, 0, style_.top_height, status.battery_level);
    DrawIconWithText(gfx, epd, status.wifi_connected ? "wifi_on.bin" : "wifi_off.bin", "", wifi_right, 0, style_.top_height, status_baseline, style_.status_font, 4, nullptr);

    if (!status.time_text.empty()) {
        epd->DrawUtf8(style_.padding, status_baseline, status.time_text, style_.status_font, GxEPD_BLACK);
    }

    // 上栏分割线取消
   // gfx.drawFastHLine(0, style_.top_height, screen_w, GxEPD_BLACK);
    // 下栏分割线保留
    gfx.drawFastHLine(0, static_cast<int16_t>(screen_h - style_.bottom_height), screen_w, GxEPD_BLACK);

    // Bottom bar
    if (!footer_text.empty()) {
        const int16_t footer_font_ascent = GetFontAscent(style_.status_font);
        const int16_t footer_font_height = GetFontHeight(style_.status_font);
        const int16_t footer_baseline = static_cast<int16_t>(screen_h - (style_.bottom_height - footer_font_height) / 2 - footer_font_height + footer_font_ascent);
        const int16_t footer_width = epd->MeasureUtf8Width(footer_text, style_.status_font);
        const int16_t footer_x = static_cast<int16_t>((screen_w - footer_width) / 2);
        epd->DrawUtf8(footer_x, footer_baseline, footer_text, style_.status_font, GxEPD_BLACK);
    }

    // App grid
    if (items.empty()) {
        return;
    }

    const int16_t grid_w = static_cast<int16_t>(layout.columns * layout.cell_w + (layout.columns - 1) * style_.col_gap);
    const int16_t grid_h = static_cast<int16_t>(layout.rows * layout.cell_h + (layout.rows - 1) * style_.row_gap);
    const int16_t start_x = static_cast<int16_t>(layout.area_x + (layout.area_w - grid_w) / 2);
    const int16_t start_y = static_cast<int16_t>(layout.area_y + (layout.area_h - grid_h) / 2);

    const int16_t label_ascent = GetFontAscent(style_.label_font);

    const int page = (layout.items_per_page > 0) ? (selected_index / layout.items_per_page) : 0;
    const int start_index = page * layout.items_per_page;
    const int end_index = std::min<int>(static_cast<int>(items.size()), start_index + layout.items_per_page);

    for (int idx = start_index; idx < end_index; ++idx) {
        const int local = idx - start_index;
        const int row = local / layout.columns;
        const int col = local % layout.columns;
        const int16_t cell_x = static_cast<int16_t>(start_x + col * (layout.cell_w + style_.col_gap));
        const int16_t cell_y = static_cast<int16_t>(start_y + row * (layout.cell_h + style_.row_gap));

        const auto& item = items[idx];
        BinImage icon;
        bool has_icon = LoadBinImage(item.icon, &icon);
        int16_t icon_w = has_icon ? static_cast<int16_t>(icon.width) : 0;
        int16_t icon_h = has_icon ? static_cast<int16_t>(icon.height) : 0;
        const int16_t icon_x = static_cast<int16_t>(cell_x + (style_.icon_cell_w - icon_w) / 2);
        const int16_t icon_y = static_cast<int16_t>(cell_y + (style_.icon_cell_h - icon_h) / 2);

        if (has_icon && icon.data && icon_w > 0 && icon_h > 0) {
            gfx.drawBitmap(icon_x, icon_y, icon.data, icon_w, icon_h, GxEPD_BLACK);
        }

        const bool is_selected = (idx == selected_index);
        if (is_selected) {
            const int16_t rect_w = has_icon ? static_cast<int16_t>(icon_w + 6) : style_.icon_cell_w;
            const int16_t rect_h = has_icon ? static_cast<int16_t>(icon_h + 6) : style_.icon_cell_h;
            const int16_t rect_x = has_icon ? static_cast<int16_t>(icon_x - 3) : cell_x;
            const int16_t rect_y = has_icon ? static_cast<int16_t>(icon_y - 3) : cell_y;
            DrawSelectionRect(gfx, rect_x, rect_y, rect_w, rect_h, style_.selection_border);
        }

        if (!item.meta.title.empty()) {
            const int16_t label_y = static_cast<int16_t>(cell_y + style_.icon_cell_h + style_.icon_label_gap + label_ascent + 4);
            if (is_selected) {
                const int16_t star_d = 16;
                const int16_t star_r = static_cast<int16_t>(star_d / 2);
                const int16_t star_gap = 4;
                const int16_t text_w = epd->MeasureUtf8Width(item.meta.title, style_.label_font);
                const int16_t total_w = static_cast<int16_t>(star_d + star_gap + text_w);
                const int16_t label_x = static_cast<int16_t>(cell_x + (layout.cell_w - total_w) / 2);
                const int16_t star_center_x = static_cast<int16_t>(label_x + star_r);
                const int16_t text_h = GetFontHeight(style_.label_font);
                const int16_t text_center_y = static_cast<int16_t>(label_y - label_ascent + text_h / 2);
                const int16_t star_center_y = text_center_y;
                const int16_t text_x = static_cast<int16_t>(label_x + star_d + star_gap);
                const int16_t inner_r = std::max<int16_t>(1, static_cast<int16_t>(star_r * 0.5f));
                DrawFivePointStar(gfx, star_center_x, star_center_y, star_r, inner_r, GxEPD_BLACK);
                epd->DrawUtf8(text_x, label_y, item.meta.title, style_.label_font, GxEPD_BLACK);
            } else {
                const int16_t label_width = epd->MeasureUtf8Width(item.meta.title, style_.label_font);
                const int16_t label_x = static_cast<int16_t>(cell_x + (layout.cell_w - label_width) / 2);
                epd->DrawUtf8(label_x, label_y, item.meta.title, style_.label_font, GxEPD_BLACK);
            }
        }
    }
}

void MenuController::SetLayout(MenuLayout layout, int item_count) {
    layout_ = layout;
    item_count_ = std::max(0, item_count);
    if (selected_index_ >= item_count_) {
        selected_index_ = std::max(0, item_count_ - 1);
    }
}

void MenuController::SetSelected(int index) {
    if (item_count_ <= 0) {
        selected_index_ = 0;
        return;
    }
    selected_index_ = std::max(0, std::min(index, item_count_ - 1));
}

bool MenuController::Move(AppButton direction) {
    if (item_count_ <= 0) {
        return false;
    }
    if (layout_.columns <= 0) {
        layout_.columns = 1;
    }

    const int max_index = item_count_ - 1;
    int row = selected_index_ / layout_.columns;
    int col = selected_index_ % layout_.columns;
    const int max_row = max_index / layout_.columns;

    switch (direction) {
    case AppButton::Left:
        if (col > 0) {
            col -= 1;
        } else if (row > 0) {
            row -= 1;
            col = layout_.columns - 1;
        } else {
            row = max_row;
            col = max_index % layout_.columns;
        }
        break;
    case AppButton::Right:
        if ((row * layout_.columns + col + 1) <= max_index && col + 1 < layout_.columns) {
            col += 1;
        } else if (row < max_row) {
            row += 1;
            col = 0;
        } else {
            row = 0;
            col = 0;
        }
        break;
    case AppButton::Up:
    case AppButton::VolumeUp:
        if (row > 0) {
            row -= 1;
        } else {
            row = max_row;
        }
        break;
    case AppButton::Down:
    case AppButton::VolumeDown:
        if (row < max_row) {
            row += 1;
        } else {
            row = 0;
        }
        break;
    default:
        return false;
    }

    int new_index = row * layout_.columns + col;
    if (new_index > max_index) {
        new_index = max_index;
    }

    if (new_index == selected_index_) {
        return false;
    }
    selected_index_ = new_index;
    return true;
}

} // namespace eteacher::app_menu
