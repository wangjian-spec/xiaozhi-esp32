
#include "custom_epd_display.h"

#include <esp_log.h>

namespace english_teacher {

namespace {
constexpr const char* kTag = "CustomEpdDisplay";
}

CustomEpdDisplay::CustomEpdDisplay(EpdPins pins)
    : pins_(pins), display_(EpdDriver(pins.cs, pins.dc, pins.rst, pins.busy)) {
}

void CustomEpdDisplay::EnsureArduinoCore() {
    // SdFat / GxEPD2 属于 Arduino 生态库：需要 Arduino core 已初始化。
    static bool s_inited = false;
    if (!s_inited) {
        initArduino();
        s_inited = true;
    }
}

void CustomEpdDisplay::EnsureSharedSpi() {
    // EnglishTeacher：SD 与 EPD 共用 SPI，总线参数应统一。
    // SS 传 -1：不使用硬件 SS，每个外设使用各自 CS。
    static bool s_spi_inited = false;
    if (!s_spi_inited) {
        SPI.begin(SPI_PIN_NUM_CLK, SPI_PIN_NUM_MISO, SPI_PIN_NUM_MOSI, -1);
        s_spi_inited = true;
    }
}

EpdPins CustomEpdDisplay::DefaultPins() {
    return EpdPins{
        .cs = (int16_t) EPD_PIN_NUM_CS,
        .dc = (int16_t) EPD_PIN_NUM_DC,
        .rst = (int16_t) EPD_PIN_NUM_RST,
        .busy = (int16_t) EPD_PIN_NUM_BUSY,
    };
}

CustomEpdDisplay& CustomEpdDisplay::GetEpd() {
    // 用函数内 static 保证全局唯一、按需初始化。
    static CustomEpdDisplay s_epd(DefaultPins());
    return s_epd;
}

bool CustomEpdDisplay::Init(uint32_t serial_diag_bitrate,
                           bool initial,
                           uint16_t reset_duration,
                           bool pulldown_rst_mode,
                           bool clear_screen) {
    if (inited_) {
        return true;
    }

    if (pins_.cs < 0 || pins_.dc < 0 || pins_.rst < 0 || pins_.busy < 0) {
        ESP_LOGE(kTag, "EPD init failed: invalid pins (cs=%d dc=%d rst=%d busy=%d)", pins_.cs, pins_.dc, pins_.rst, pins_.busy);
        return false;
    }

    // 关键点：EPD 和 SD 共享 SPI。
    // 这里仅确保 CS 初始为非选中态，避免上电后占用总线。
    pinMode(pins_.cs, OUTPUT);
    digitalWrite(pins_.cs, HIGH);
    pinMode(pins_.dc, OUTPUT);
    pinMode(pins_.rst, OUTPUT);
    pinMode(pins_.busy, INPUT);

    // 使用与 boards/EnglishTeacher/english-teacher.cc 一致的 init 参数。
    display_.init(serial_diag_bitrate, initial, reset_duration, pulldown_rst_mode);

    if (clear_screen) {
        // 可选：启动后做一次“白底全刷”，避免残影/脏屏。
        display_.fillScreen(GxEPD_WHITE);
        display_.display(false);
    }

    inited_ = true;
    ESP_LOGI(kTag, "EPD init OK (GxEPD2)");
    return true;
}

void CustomEpdDisplay::SetRotation(uint8_t rotation) {
    display_.setRotation(rotation);
}

void CustomEpdDisplay::FillWhite() {
    display_.fillScreen(GxEPD_WHITE);
}

void CustomEpdDisplay::FillBlack() {
    display_.fillScreen(GxEPD_BLACK);
}

void CustomEpdDisplay::DisplayFull() {
    display_.display(false);
}

void CustomEpdDisplay::DisplayPartial() {
    display_.display(true);
}

void CustomEpdDisplay::DisplayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    display_.displayWindow(x, y, w, h);
}

void CustomEpdDisplay::Hibernate() {
    // 深度休眠：仅在 rst>=0 的硬件上可被 RST 唤醒。
    display_.hibernate();
}

EpdDisplay& CustomEpdDisplay::Raw() {
    return display_;
}

} // namespace english_teacher
