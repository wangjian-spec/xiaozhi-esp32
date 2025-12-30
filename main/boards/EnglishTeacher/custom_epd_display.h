#ifndef CUSTOM_EPD_DISPLAY_H
#define CUSTOM_EPD_DISPLAY_H

// 说明：
// - 本文件不再使用微雪示例里“手写 SPI 指令 / LUT / buffer”的自定义接口。
// - 统一改为 Arduino 生态的 GxEPD2 + Adafruit_GFX API。
// - 目标：接口简单、易维护；上层如果需要更高级能力，可直接拿到底层 GxEPD2 对象。

#include <stdint.h>

#include <Arduino.h>
#include <SPI.h>

#include <GxEPD2_BW.h>

#include "config.h"

namespace english_teacher {

// GxEPD2 必须在编译期选择具体面板型号。
// 该选择与 pins 定义位于 main/boards/EnglishTeacher/config.h
#if defined(ENGLISH_TEACHER_EPD_PANEL_GDEY042T81) && (ENGLISH_TEACHER_EPD_PANEL_GDEY042T81 == 1)
    #include <gdey/GxEPD2_420_GDEY042T81.h>
    using EpdDriver = GxEPD2_420_GDEY042T81;
#else
    #error "Please select an EPD panel in main/boards/EnglishTeacher/config.h (e.g. ENGLISH_TEACHER_EPD_PANEL_GDEY042T81)"
#endif

// 使用整屏 buffer（page_height = HEIGHT）：
// - 方便做 drawPixel()/displayWindow()（LVGL flush 或自绘都直观）
// - 内存开销可控：典型 4.2"(400x300) 约 15KB
using EpdDisplay = GxEPD2_BW<EpdDriver, EpdDriver::HEIGHT>;

struct EpdPins {
    int16_t cs;
    int16_t dc;
    int16_t rst;
    int16_t busy;
};

// 一个薄封装：
// - 只做“板级/工程级”需要的初始化兜底（Arduino core / shared SPI）
// - 具体绘制能力全部交给 GxEPD2 / Adafruit_GFX
class CustomEpdDisplay {
public:
    explicit CustomEpdDisplay(EpdPins pins);

    // 建议调用顺序：Init() -> fillScreen()/drawXXX() -> display()/displayWindow()
    // serial_diag_bitrate=0：关闭串口诊断输出
    // reset_duration=2：适配常见 Waveshare "clever reset" 电路（GxEPD2 推荐）
    // 注意：此函数假定 Arduino core 与共享 SPI 已就绪。
    // EnglishTeacher 板级推荐顺序：EnsureArduinoCore() -> EnsureSharedSpi() -> (SdFat) -> Init().
    // clear_screen=true 会做一次白底全刷（启动更慢，但更干净）；默认关闭以加快启动。
    bool Init(uint32_t serial_diag_bitrate = 0,
              bool initial = true,
              uint16_t reset_duration = 2,
              bool pulldown_rst_mode = false,
              bool clear_screen = false);

    // 常用便捷 API（内部直接转发到 GxEPD2）：
    void SetRotation(uint8_t rotation);
    void FillWhite();
    void FillBlack();
    void DisplayFull();                 // display(false)
    void DisplayPartial();              // display(true)
    void DisplayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void Hibernate();

    // 允许上层直接调用完整的 GxEPD2/Adafruit_GFX API（推荐的扩展方式）
    EpdDisplay& Raw();

    // 默认 pins 来自 config.h
    static EpdPins DefaultPins();

    // EnglishTeacher：SD 与 EPD 共用 SPI，总线初始化要统一。
    // 若板级已调用 SPI.begin(...)，可不调用此函数。
    static void EnsureArduinoCore();
    static void EnsureSharedSpi();

    // EnglishTeacher 推荐用法：全局唯一 EPD 对象
    // - 避免多个模块各自 new / init 导致的重复复位、SPI 争用
    // - 统一由板级初始化一次，其他模块按需取引用使用
    static CustomEpdDisplay& GetEpd();

private:
    const EpdPins pins_;
    EpdDisplay display_;
    bool inited_ = false;

};

} // namespace english_teacher

#endif // CUSTOM_EPD_DISPLAY_H