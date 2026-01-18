#include "eteacher/app_manager/menu.h"

#include <algorithm>
#include <Adafruit_GFX.h>
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
    for (int i = 0; i < border; ++i) {
        gfx.drawRect(x - i, y - i, w + i * 2, h + i * 2, GxEPD_BLACK);
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

    const auto layout = ComputeLayout(screen_w, screen_h, items.size());

    // Top bar
    const int16_t status_font_ascent = GetFontAscent(style_.status_font);
    const int16_t status_font_height = GetFontHeight(style_.status_font);
    const int16_t status_baseline = static_cast<int16_t>((style_.top_height - status_font_height) / 2 + status_font_ascent);

    int16_t right_cursor = static_cast<int16_t>(screen_w - style_.padding);
    DrawIconWithText(gfx, epd, "volume.bin", status.volume_text, right_cursor, 0, style_.top_height, status_baseline, style_.status_font, 4, &right_cursor);
    right_cursor = static_cast<int16_t>(right_cursor - style_.col_gap);
    DrawIconWithText(gfx, epd, "battery.bin", status.battery_text, right_cursor, 0, style_.top_height, status_baseline, style_.status_font, 4, &right_cursor);
    right_cursor = static_cast<int16_t>(right_cursor - style_.col_gap);
    DrawIconWithText(gfx, epd, "wifi.bin", status.wifi_text, right_cursor, 0, style_.top_height, status_baseline, style_.status_font, 4, &right_cursor);

    if (!status.time_text.empty()) {
        epd->DrawUtf8(style_.padding, status_baseline, status.time_text, style_.status_font, GxEPD_BLACK);
    }

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

        if (idx == selected_index) {
            const int16_t rect_w = has_icon ? icon_w : style_.icon_cell_w;
            const int16_t rect_h = has_icon ? icon_h : style_.icon_cell_h;
            const int16_t rect_x = has_icon ? icon_x : cell_x;
            const int16_t rect_y = has_icon ? icon_y : cell_y;
            DrawSelectionRect(gfx, rect_x, rect_y, rect_w, rect_h, style_.selection_border);
        }

        if (!item.meta.title.empty()) {
            const int16_t label_width = epd->MeasureUtf8Width(item.meta.title, style_.label_font);
            const int16_t label_x = static_cast<int16_t>(cell_x + (layout.cell_w - label_width) / 2);
            const int16_t label_y = static_cast<int16_t>(cell_y + style_.icon_cell_h + style_.icon_label_gap + label_ascent);
            epd->DrawUtf8(label_x, label_y, item.meta.title, style_.label_font, GxEPD_BLACK);
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
