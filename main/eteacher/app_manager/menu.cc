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
#include "eteacher/app_ui/status_bar.h"

namespace eteacher::app_menu {
using namespace eteacher::app_ui;

void DrawSelectionRect(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t border) {
    if (w <= 0 || h <= 0 || border <= 0) return;
    int16_t radius = static_cast<int16_t>(std::min<int16_t>(8, std::min<int16_t>(w, h) / 4));
    for (int i = 0; i < border; ++i) {
        gfx.drawRoundRect(x - i, y - i, w + i * 2, h + i * 2, radius + i, GxEPD_BLACK);
    }
}

void DrawFivePointStar(Adafruit_GFX& gfx,
                       int16_t center_x,
                       int16_t center_y,
                       int16_t outer_r,
                       int16_t inner_r,
                       uint16_t color) {
    if (outer_r <= 0 || inner_r <= 0) return;
    struct Point { int16_t x; int16_t y; };
    std::array<Point, 10> pts{};
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kDegToRad = kPi / 180.0f;
    for (int i = 0; i < 5; ++i) {
        const float outer_angle = (-90.0f + i * 72.0f) * kDegToRad;
        const float inner_angle = (-90.0f + i * 72.0f + 36.0f) * kDegToRad;
        pts[i * 2] = { static_cast<int16_t>(std::lround(center_x + outer_r * std::cos(outer_angle))),
                       static_cast<int16_t>(std::lround(center_y + outer_r * std::sin(outer_angle))) };
        pts[i * 2 + 1] = { static_cast<int16_t>(std::lround(center_x + inner_r * std::cos(inner_angle))),
                           static_cast<int16_t>(std::lround(center_y + inner_r * std::sin(inner_angle))) };
    }
    for (int i = 0; i < 10; ++i) {
        const auto& p0 = pts[i];
        const auto& p1 = pts[(i + 1) % 10];
        gfx.drawLine(p0.x, p0.y, p1.x, p1.y, color);
    }
}

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
// Draw the menu with given items and selection.
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

    // Top/Bottom bars (delegated)
    eteacher::app_ui::DrawTopBottomBars(gfx, epd, style_, status, footer_text);

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
//当菜单格局或项目数量发生变化时调用它，用来同步布局和项目计数
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
