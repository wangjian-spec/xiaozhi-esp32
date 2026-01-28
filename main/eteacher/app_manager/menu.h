#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "eteacher/app_manager/app_base.h"
// status_bar definitions moved into menu.h to centralize menu-related types.

class Adafruit_GFX;
class CustomEpdDisplay;

namespace eteacher::app_menu {

// Top bar style (single source of truth).
struct MenuStyle {
    int16_t top_height = 20;
    int16_t bottom_height = 16;
    int16_t padding = 8;

    int16_t icon_cell_w = 80;
    int16_t icon_cell_h = 80;
    int16_t col_gap = 14;
    int16_t row_gap = 14;
    int16_t icon_label_gap = 4;
    int16_t selection_border = 2;

    std::string label_font = "wenquanyi_9pt";
    std::string status_font = "wenquanyi_9pt";
    std::string title_font = "wenquanyi_11pt";
};

// Top bar status content (single source of truth).
struct MenuStatus {
    std::string time_text;
    bool wifi_connected = false;
    std::string battery_text;
    int battery_level = -1;
    std::string volume_text;
};

struct MenuItem {
    MenuMeta meta;
    std::string icon;
};

struct MenuLayout {
    int16_t area_x = 0;
    int16_t area_y = 0;
    int16_t area_w = 0;
    int16_t area_h = 0;
    int16_t columns = 1;
    int16_t rows = 1;
    int16_t items_per_page = 1;
    int16_t cell_w = 0;
    int16_t cell_h = 0;
    int16_t label_height = 0;
};

class Menu {
public:
    Menu() = default;
    explicit Menu(MenuStyle style) : style_(std::move(style)) {}

    void SetStyle(MenuStyle style) { style_ = std::move(style); }
    const MenuStyle& style() const { return style_; }

    //声明成员函数，返回值类型为MenuLayout，函数名为ComputeLayout，参数为int16_t类型的screen_w、int16_t类型的screen_h和size_t类型的item_count，函数为常量成员函数
    MenuLayout ComputeLayout(int16_t screen_w, int16_t screen_h, size_t item_count) const;

    void Draw(Adafruit_GFX& gfx,
              CustomEpdDisplay* epd,
              const std::vector<MenuItem>& items,
              int selected_index,
              const MenuStatus& status,
              std::string_view footer_text) const;

private:
    MenuStyle style_{};
};

class MenuController {
public:
    void SetLayout(MenuLayout layout, int item_count);
    void SetSelected(int index);
    int selected() const { return selected_index_; }

    bool Move(AppButton direction);

private:
    MenuLayout layout_{};
    int item_count_ = 0;
    int selected_index_ = 0;
};

} // namespace eteacher::app_menu
