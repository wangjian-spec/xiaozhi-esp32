#pragma once

#include <string_view>

class Adafruit_GFX;
class CustomEpdDisplay;

namespace eteacher::app_menu {
struct MenuStyle;
struct MenuStatus;
}

namespace eteacher::app_service::tool {

void DrawTopBar(Adafruit_GFX& gfx,
                CustomEpdDisplay* epd,
                const eteacher::app_menu::MenuStyle& style,
                const eteacher::app_menu::MenuStatus& status);

void DrawBottomBar(Adafruit_GFX& gfx,
                   CustomEpdDisplay* epd,
                   const eteacher::app_menu::MenuStyle& style,
                   std::string_view footer_text);

// Shared helpers used by status bar and menu rendering
struct BinImage {
    const uint8_t* data = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    size_t data_size = 0;
};

bool LoadBinImage(const std::string& name, BinImage* out);

int16_t GetFontHeight(std::string_view font_name);
int16_t GetFontAscent(std::string_view font_name);

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
                      int16_t* out_left_x = nullptr);

void DrawBatteryIcon(Adafruit_GFX& gfx, int16_t right_x, int16_t top_y, int16_t bar_h, int level);

} // namespace eteacher::app_service::tool
