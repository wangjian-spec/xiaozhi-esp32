#pragma once

#include <string>
#include <string_view>

#include <wifi_manager.h>

#include "eteacher/app_manager/menu.h"

class Adafruit_GFX;
class CustomEpdDisplay;
class Board;

namespace eteacher::app_ui {

void DrawTopBar(Adafruit_GFX& gfx,
                CustomEpdDisplay* epd,
                const eteacher::app_menu::MenuStyle& style,
                const eteacher::app_menu::MenuStatus& status);

void DrawBottomBar(Adafruit_GFX& gfx,
                   CustomEpdDisplay* epd,
                   const eteacher::app_menu::MenuStyle& style,
                   std::string_view footer_text);

// Convenience wrapper to draw both bars with shared style.
void DrawTopBottomBars(Adafruit_GFX& gfx,
                       CustomEpdDisplay* epd,
                       const eteacher::app_menu::MenuStyle& style,
                       const eteacher::app_menu::MenuStatus& status,
                       std::string_view footer_text);

// Unified status content helpers (used by all apps/menus).
std::string FormatTimeText();
int GetCurrentMinuteOfDay();
std::string FormatBatteryText(Board &board);
int GetBatteryLevelPercent(Board &board);
std::string FormatVolumeText(Board &board);
inline eteacher::app_menu::MenuStatus BuildMenuStatus(Board &board) {
    eteacher::app_menu::MenuStatus status;
    status.time_text = FormatTimeText();
    status.wifi_connected = WifiManager::GetInstance().IsConnected();
    status.battery_text = FormatBatteryText(board);
    status.battery_level = GetBatteryLevelPercent(board);
    status.volume_text = FormatVolumeText(board);
    return status;
}

// Check current board status and update `last_status` if something changed.
// Returns true when at least one of (time, wifi connection, battery level)
// has changed and `last_status` was updated to the new values.
bool CheckAndUpdateMenuStatus(Board &board, eteacher::app_menu::MenuStatus &last_status);

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

} // namespace eteacher::app_ui
