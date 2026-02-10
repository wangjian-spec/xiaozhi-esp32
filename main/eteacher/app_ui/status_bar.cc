// 状态栏绘制实现
// 本文件实现顶部/底部状态栏相关的图标、文本绘制及资源加载逻辑，负责在屏幕上显示电量、时间、音量等状态信息。

#include "eteacher/app_ui/status_bar.h"

#include <Adafruit_GFX.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <vector>

#include "assets.h"
#include "audio/audio_codec.h"
#include "boards/common/board.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/font_manager/font_manager.h"
#include <esp_log.h>


namespace eteacher::app_ui {
namespace {

static constexpr char kTag[] = "StatusBar";

static inline uint16_t ReadLE16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}
} // anonymous

// Implementations of helpers declared in status_bar.h (use anonymous ReadLE16/kTag above)
bool LoadBinImage(const std::string& name, BinImage* out) {
    if (!out) return false;
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
        ESP_LOGW(kTag, "Icon size mismatch: %s", name.c_str());
        static std::vector<uint8_t> scratch;
        if (scratch.size() < bytes) scratch.resize(bytes);
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
    if (!font || !font->Ready()) return 12;
    const auto& header = font->Header();
    return static_cast<int16_t>(header.ascent + header.descent);
}

int16_t GetFontAscent(std::string_view font_name) {
    const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
    if (!font || !font->Ready()) return 9;
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
        if (out_left_x) *out_left_x = right_x;
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
    if (out_left_x) *out_left_x = left_x;
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

    if (level < 0) return;
    const int scaled2 = level * 2;
    int blocks = 0;
    if (scaled2 < 25) blocks = 0;
    else if (scaled2 < 75) blocks = 1;
    else if (scaled2 < 125) blocks = 2;
    else if (scaled2 < 175) blocks = 3;
    else blocks = 4;
    const int16_t padding = 1;
    const int16_t inner_x = static_cast<int16_t>(x + padding);
    const int16_t inner_y = static_cast<int16_t>(y + padding);
    const int16_t inner_w = static_cast<int16_t>(battery_w - padding * 2);
    const int16_t inner_h = static_cast<int16_t>(battery_h - padding * 2);
    const int16_t gap = 1;
    gfx.fillRect(inner_x, inner_y, inner_w, inner_h, GxEPD_WHITE);
    const int16_t block_w = static_cast<int16_t>((inner_w - gap * 3) / 4);
    if (block_w <= 0 || inner_h <= 0) return;
    for (int i = 0; i < blocks; ++i) {
        const int16_t bx = static_cast<int16_t>(inner_x + i * (block_w + gap));
        gfx.fillRect(bx, inner_y, block_w, inner_h, GxEPD_BLACK);
    }
}

std::string FormatTimeText() {
    std::time_t now = std::time(nullptr);
    if (now <= 0) {
        return "----年--月--日 星期-  --:--";
    }
    std::tm local_tm{};
    localtime_r(&now, &local_tm);
    static const char* kWeekday[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
    const char* weekday = "星期-";
    if (local_tm.tm_wday >= 0 && local_tm.tm_wday < 7) {
        weekday = kWeekday[local_tm.tm_wday];
    }
    char buf[64] = {0};
    std::snprintf(buf,
                  sizeof(buf),
                  "%04d年%02d月%02d日 %s  %02d:%02d",
                  local_tm.tm_year + 1900,
                  local_tm.tm_mon + 1,
                  local_tm.tm_mday,
                  weekday,
                  local_tm.tm_hour,
                  local_tm.tm_min);
    return std::string(buf);
}

int GetCurrentMinuteOfDay() {
    std::time_t now = std::time(nullptr);
    if (now <= 0) {
        return -1;
    }
    std::tm local_tm{};
    localtime_r(&now, &local_tm);
    return local_tm.tm_hour * 60 + local_tm.tm_min;
}

std::string FormatBatteryText(Board &board) {
    int level = 0;
    bool charging = false;
    bool discharging = false;
    if (!board.GetBatteryLevel(level, charging, discharging)) {
        return "--";
    }
    std::string text = std::to_string(level) + "%";
    if (charging) {
        text += "+";
    }
    return text;
}

int GetBatteryLevelPercent(Board &board) {
    int level = 0;
    bool charging = false;
    bool discharging = false;
    if (!board.GetBatteryLevel(level, charging, discharging)) {
        return 50;
    }
    if (level < 0) return 0;
    if (level > 100) return 100;
    return level;
}

std::string FormatVolumeText(Board &board) {
    auto* codec = board.GetAudioCodec();
    if (!codec) {
        return "--";
    }
    return std::to_string(codec->output_volume()) + "%";
}

bool CheckAndUpdateMenuStatus(Board &board, eteacher::app_menu::MenuStatus &last_status) {
    const auto new_status = BuildMenuStatus(board);
    // Compare the fields we care about for driving menu refreshes.
    const bool time_changed = (new_status.time_text != last_status.time_text);
    const bool wifi_changed = (new_status.wifi_connected != last_status.wifi_connected);
    const bool battery_changed = (new_status.battery_level != last_status.battery_level);
    const bool volume_changed = (new_status.volume_text != last_status.volume_text);

    if (time_changed || wifi_changed || battery_changed || volume_changed) {
        last_status = new_status;
        return true;
    }
    return false;
}

void DrawTopBar(Adafruit_GFX& gfx,
                CustomEpdDisplay* epd,
                const eteacher::app_menu::MenuStyle& style,
                const eteacher::app_menu::MenuStatus& status) {
    if (!epd) return;
    // Screen dimensions
    const int16_t screen_w = static_cast<int16_t>(epd->width());
    const int16_t screen_h = static_cast<int16_t>(epd->height());

    // Draw outer screen rectangle frame
    gfx.drawRect(0, 0, screen_w, screen_h, GxEPD_BLACK);

    const int16_t status_font_ascent = GetFontAscent(style.status_font);
    const int16_t status_font_height = GetFontHeight(style.status_font);
    const int16_t status_baseline = static_cast<int16_t>((style.top_height - status_font_height) / 2 + status_font_ascent);

    const int16_t vol_batt_gap = 50;
    const int16_t batt_wifi_gap = 35;
    const int16_t status_right = static_cast<int16_t>(screen_w - style.padding);
    const int16_t volume_group_right = status_right;
    const int16_t battery_right = static_cast<int16_t>(status_right - vol_batt_gap);
    const int16_t wifi_right = static_cast<int16_t>(battery_right - batt_wifi_gap);

    BinImage volume_icon;
    bool has_volume = LoadBinImage("volume.bin", &volume_icon);
    if (has_volume && volume_icon.data && volume_icon.width > 0 && volume_icon.height > 0) {
        const int16_t icon_w = static_cast<int16_t>(volume_icon.width);
        const int16_t icon_h = static_cast<int16_t>(volume_icon.height);
        const int16_t icon_text_gap = 4;
        const int16_t fixed_text_w = 20;
        const int16_t text_w = status.volume_text.empty() ? 0 : fixed_text_w;
        const int16_t total_w = icon_w + (text_w > 0 ? (icon_text_gap + text_w) : 0);
        const int16_t icon_x = static_cast<int16_t>(volume_group_right - total_w);
        const int16_t icon_y = static_cast<int16_t>((style.top_height - icon_h) / 2);
        gfx.drawBitmap(icon_x, icon_y, volume_icon.data, icon_w, icon_h, GxEPD_BLACK);
        if (!status.volume_text.empty()) {
            const int16_t area_x = static_cast<int16_t>(icon_x + icon_w + icon_text_gap);
            const int16_t area_right = static_cast<int16_t>(area_x + text_w);
            const int16_t measured_w = epd->MeasureUtf8Width(status.volume_text, style.status_font);
            const int16_t draw_x = static_cast<int16_t>(std::max<int16_t>(area_x, area_right - measured_w));
            epd->DrawUtf8(draw_x, status_baseline, status.volume_text, style.status_font, GxEPD_BLACK);
        }
    } else {
        const int16_t fixed_text_w = 30;
        const int16_t text_w = status.volume_text.empty() ? 0 : fixed_text_w;
        if (text_w > 0) {
            const int16_t area_x = static_cast<int16_t>(volume_group_right - text_w);
            const int16_t area_right = static_cast<int16_t>(volume_group_right);
            const int16_t measured_w = epd->MeasureUtf8Width(status.volume_text, style.status_font);
            const int16_t draw_x = static_cast<int16_t>(std::max<int16_t>(area_x, area_right - measured_w));
            epd->DrawUtf8(draw_x, status_baseline, status.volume_text, style.status_font, GxEPD_BLACK);
        }
    }

    DrawBatteryIcon(gfx, battery_right, 0, style.top_height, status.battery_level);
    DrawIconWithText(gfx, epd, status.wifi_connected ? std::string("wifi_on.bin") : std::string("wifi_off.bin"), "", wifi_right, 0, style.top_height, status_baseline, style.status_font, 4);

    if (!status.time_text.empty()) {
        epd->DrawUtf8(style.padding, status_baseline, status.time_text, style.status_font, GxEPD_BLACK);
    }
}

void DrawBottomBar(Adafruit_GFX& gfx, CustomEpdDisplay* epd, const eteacher::app_menu::MenuStyle& style, std::string_view footer_text) {
    if (!epd) return;
    const int16_t screen_h = static_cast<int16_t>(epd->height());
    const int16_t screen_w = static_cast<int16_t>(epd->width());
    // draw bottom divider line
    gfx.drawFastHLine(0, static_cast<int16_t>(screen_h - style.bottom_height), screen_w, GxEPD_BLACK);
    if (!footer_text.empty()) {
        const int16_t footer_font_ascent = GetFontAscent(style.status_font);
        const int16_t footer_font_height = GetFontHeight(style.status_font);
        const int16_t footer_baseline = static_cast<int16_t>(screen_h - (style.bottom_height - footer_font_height) / 2 - footer_font_height + footer_font_ascent);
        const int16_t footer_width = epd->MeasureUtf8Width(footer_text, style.status_font);
        const int16_t footer_x = static_cast<int16_t>((static_cast<int>(screen_w) - footer_width) / 2);
        epd->DrawUtf8(footer_x, footer_baseline, footer_text, style.status_font, GxEPD_BLACK);
    }
}

void DrawTopBottomBars(Adafruit_GFX& gfx,
                       CustomEpdDisplay* epd,
                       const eteacher::app_menu::MenuStyle& style,
                       const eteacher::app_menu::MenuStatus& status,
                       std::string_view footer_text) {
    DrawTopBar(gfx, epd, style, status);
    DrawBottomBar(gfx, epd, style, footer_text);
}

} // namespace eteacher::app_ui
