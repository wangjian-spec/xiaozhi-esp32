#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "eteacher/app_manager/app_base.h"

class Adafruit_GFX;
class CustomEpdDisplay;

namespace eteacher::app_menu {

struct MenuItem {
    MenuMeta meta;
    std::string icon;
};

struct MenuStatus {
    std::string time_text;
    std::string wifi_text;
    std::string battery_text;
    std::string volume_text;
};

struct MenuStyle {
    int16_t top_height = 24;
    int16_t bottom_height = 24;
    int16_t padding = 8;

    int16_t icon_cell_w = 80;
    int16_t icon_cell_h = 80;
    int16_t col_gap = 12;
    int16_t row_gap = 12;
    int16_t icon_label_gap = 4;
    int16_t selection_border = 2;

    std::string label_font = "wenquanyi_9pt";
    std::string status_font = "wenquanyi_9pt";
    std::string title_font = "wenquanyi_11pt";
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
