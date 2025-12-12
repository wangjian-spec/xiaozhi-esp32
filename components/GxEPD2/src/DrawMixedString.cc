#include "esp_log.h"
#include <string.h>
#include<driver_gt30l32s4w.h>
#include<driver_gt30l32s4w_basic.h>
#include<driver_gt30l32s4w_interface.h>
#include<utf8_to_gb2312_table.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include "DrawMixedString.h"

static const char *TAG = "EPD_DEMO";
static gt30l32s4w_handle_t gs_handle;        /**< gt30l32s4w handle */


DisplayClass display(   
    GxEPD2_DRIVER_CLASS(
        /*CS=*/ EPD_PIN_NUM_CS,
        /*DC=*/ EPD_PIN_NUM_DC,
        /*RST=*/ EPD_PIN_NUM_RST,
        /*BUSY=*/ EPD_PIN_NUM_BUSY
    )
);

namespace {
constexpr uint32_t kEpdSpiFreq = 20000000UL;  // 20 MHz for e-ink and GT30 transfers
const SPISettings kEpdSpiSettings{kEpdSpiFreq, MSBFIRST, SPI_MODE0};
bool EnsureEpdSpiBus() {
    static bool spi_ready = false;
    if (!spi_ready) {
        SPI.begin(SPI_PIN_NUM_CLK, SPI_PIN_NUM_MISO, SPI_PIN_NUM_MOSI);
        spi_ready = true;
        ESP_LOGI(TAG, "SPI bus configured for EPD: clk=%d miso=%d mosi=%d", SPI_PIN_NUM_CLK, SPI_PIN_NUM_MISO, SPI_PIN_NUM_MOSI);
    }
    return spi_ready;
}
}

// ...existing code...


// UTF-8 -> GB2312 表
typedef struct {
    uint8_t utf8[3];
    uint8_t gb[2];
} utf8_gb2312_t;

struct FontMetrics {
    int chinese_width;
    int chinese_height;
    int ascii_width;
    int ascii_height;
};

static draw_mixed_font_size_t normalize_font_size(draw_mixed_font_size_t font_size) {
    switch (font_size) {
        case DRAW_MIXED_FONT_12:
        case DRAW_MIXED_FONT_16:
        case DRAW_MIXED_FONT_24:
        case DRAW_MIXED_FONT_32:
            return font_size;
        default:
            ESP_LOGW(TAG, "Unsupported font size %d, fallback to 16", font_size);
            return DRAW_MIXED_FONT_16;
    }
}

static FontMetrics metrics_for(draw_mixed_font_size_t font_size) {
    switch (font_size) {
        case DRAW_MIXED_FONT_12:
            return {12, 12, 6, 12};
        case DRAW_MIXED_FONT_16:
            return {16, 16, 8, 16};
        case DRAW_MIXED_FONT_24:
            return {24, 24, 12, 24};
        case DRAW_MIXED_FONT_32:
            return {32, 32, 16, 32};
        default:
            return {16, 16, 8, 16};
    }
}



uint8_t gt30_init()
{
    uint8_t res;
    
    /* link function */
    DRIVER_GT30L32S4W_LINK_INIT(&gs_handle, gt30l32s4w_handle_t);
    DRIVER_GT30L32S4W_LINK_SPI_INIT(&gs_handle, gt30l32s4w_interface_spi_init);
    DRIVER_GT30L32S4W_LINK_SPI_DEINIT(&gs_handle, gt30l32s4w_interface_spi_deinit);
    DRIVER_GT30L32S4W_LINK_SPI_WRITE_READ(&gs_handle, gt30l32s4w_interface_spi_write_read);
    DRIVER_GT30L32S4W_LINK_DELAY_MS(&gs_handle, gt30l32s4w_interface_delay_ms);
    DRIVER_GT30L32S4W_LINK_DEBUG_PRINT(&gs_handle, gt30l32s4w_interface_debug_print);
    
    /* gt30l32s4w init */
    res = gt30l32s4w_init(&gs_handle);
    if (res != 0)
    {
        gt30l32s4w_interface_debug_print("gt30l32s4w: init failed.\n");
        
        return 1;
    }
    
     /* set default mode */
    res = gt30l32s4w_set_mode(&gs_handle, GT30L32S4W_BASIC_DEFAULT_MODE);
    if (res != 0)
    {
        gt30l32s4w_interface_debug_print("gt30l32s4w: set mode failed.\n");
        (void)gt30l32s4w_deinit(&gs_handle);
        
        return 1;
    }
    
    return 0;
}









bool utf8_to_gb2312(const char* utf8Char, uint8_t gb[2]) {
    if (!utf8Char || !gb) return false;
    for (int i = 0; i < 6763; i++) {
        if (strncmp((const char*)utf8_gb2312_table[i].utf8, utf8Char, 3) == 0) {
            gb[0] = utf8_gb2312_table[i].gb[0];
            gb[1] = utf8_gb2312_table[i].gb[1];
            return true;
        }
    }
    ESP_LOGW(TAG, "utf8_to_gb2312 fail for UTF-8: %02X %02X %02X",
             (unsigned char)utf8Char[0],
             (unsigned char)utf8Char[1],
             (unsigned char)utf8Char[2]);
    return false;
}





bool drawChinese(gt30l32s4w_handle_t *handle, uint16_t gbCode, int x, int y, draw_mixed_font_size_t font_size)
{
    uint8_t ret = 0;
    int width = 0;
    int height = 0;
    uint8_t buf[128] = {0};

    switch (font_size) {
        case DRAW_MIXED_FONT_12:
            ret = gt30l32s4w_read_char_12x12(handle, gbCode, buf);
            width = 12; height = 12;
            break;
        case DRAW_MIXED_FONT_16:
            ret = gt30l32s4w_read_char_15x16(handle, gbCode, buf);
            width = 16; height = 16;
            break;
        case DRAW_MIXED_FONT_24:
            ret = gt30l32s4w_read_char_24x24(handle, gbCode, buf);
            width = 24; height = 24;
            break;
        case DRAW_MIXED_FONT_32:
            ret = gt30l32s4w_read_char_32x32(handle, gbCode, buf);
            width = 32; height = 32;
            break;
        default:
            ret = 1;
            ESP_LOGW(TAG, "Unsupported Chinese font size %d", font_size);
            break;
    }

    if (ret != 0) {
        ESP_LOGW(TAG, "Chinese read fail, ret=%d", ret);
        return false;
    }

    display.drawBitmap(x, y, buf, width, height, GxEPD_BLACK);
    return true;
}




// 读取并显示一个 ASCII 字符
bool drawAscii(gt30l32s4w_handle_t *handle,char asciiChar, int x, int y, draw_mixed_font_size_t font_size) {
    uint8_t ret = 0;
    int width = 0;
    int height = 0;
    uint8_t buf[64] = {0};

    switch (font_size) {
        case DRAW_MIXED_FONT_12:
            ret = gt30l32s4w_read_ascii_6x12(handle, (uint16_t)asciiChar, buf);
            width = 6; height = 12;
            break;
        case DRAW_MIXED_FONT_16:
            ret = gt30l32s4w_read_ascii_8x16(handle, (uint16_t)asciiChar, buf);
            width = 8; height = 16;
            break;
        case DRAW_MIXED_FONT_24:
            ret = gt30l32s4w_read_ascii_12x24(handle, (uint16_t)asciiChar, buf);
            width = 12; height = 24;
            break;
        case DRAW_MIXED_FONT_32:
            ret = gt30l32s4w_read_ascii_16x32(handle, (uint16_t)asciiChar, buf);
            width = 16; height = 32;
            break;
        default:
            ret = 1;
            ESP_LOGW(TAG, "Unsupported ASCII font size %d", font_size);
            break;
    }

    if (ret != 0) {
        ESP_LOGW(TAG, "ascii read fail, ret=%d", ret);
        return false;
    }

    display.drawBitmap(x, y, buf, width, height, GxEPD_BLACK);
    return true;
}



bool isChineseUTF8(const char *str)
{
    unsigned char c = (unsigned char)str[0];
    return (c >= 0x80);  // 所有多字节 UTF-8，包括中文汉字和中文标点
}


void drawBitmapMixedString(const char* utf8Str, int x, int y, draw_mixed_font_size_t font_size)
{
    const draw_mixed_font_size_t normalized_size = normalize_font_size(font_size);
    const FontMetrics metrics = metrics_for(normalized_size);
    int cursorX = x;
    int cursorY = y;

    while (*utf8Str) {
        if (isChineseUTF8(utf8Str)) {
            // UTF-8 → GB2312 转换
            uint8_t gb2312[2];
            if (!utf8_to_gb2312(utf8Str, gb2312)) {
                utf8Str += 3;
                cursorX += metrics.chinese_width;
                continue;
            }
            uint16_t gbCode = (gb2312[0] << 8) | gb2312[1];

            // 显示中文
            drawChinese(&gs_handle, gbCode, cursorX, cursorY, normalized_size);
            cursorX += metrics.chinese_width;

            utf8Str += 3; // UTF-8 中文占3字节
        } else {
            // 显示英文
            drawAscii(&gs_handle, *utf8Str, cursorX, cursorY, normalized_size);
            cursorX += metrics.ascii_width;

            utf8Str += 1;
        }
    }
}

// C-compatible wrapper API so other TUs don't need to include GxEPD2 headers
extern "C" {
    // 初始化Arduino，GT30,EPD设备
    void drawMixedString_init()
    {
        // initialize GT30 and display with sensible defaults
        initArduino();
        EnsureEpdSpiBus();
        pinMode(EPD_PIN_NUM_CS, OUTPUT);
        pinMode(EPD_PIN_NUM_DC, OUTPUT);
        pinMode(EPD_PIN_NUM_RST, OUTPUT);
        pinMode(EPD_PIN_NUM_BUSY, INPUT);
        pinMode(GT30_PIN_NUM_CS, OUTPUT);
        SPI.beginTransaction(kEpdSpiSettings);
        uint8_t rt = gt30_init();
        SPI.endTransaction();
        if (rt != 0) {
            ESP_LOGE(TAG, "gt30_init failed: %d", rt);
        } else {
            ESP_LOGI(TAG, "gt30_init ok");
        }
        display.init(115200, true, 2, false);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        display.fillScreen(GxEPD_WHITE);
        // force a full refresh to ensure EPD shows the current content (white)
        display.display(false);
        ESP_LOGI(TAG, "EPD init done: width=%d height=%d", display.width(), display.height());
        display.setRotation(0);
    }

    void drawMixedString_fillScreen(int color)
    {
        display.fillScreen(color);
    }

    void drawMixedString_drawText(const char* utf8, int x, int y, draw_mixed_font_size_t font_size)
    {
        const draw_mixed_font_size_t normalized_size = normalize_font_size(font_size);
        ESP_LOGD(TAG, "drawMixedString_drawText: x=%d y=%d size=%d text=%s", x, y, normalized_size, utf8);
        drawBitmapMixedString(utf8, x, y, normalized_size);
    }

    void drawMixedString_display(bool partial)
    {
        ESP_LOGD(TAG, "drawMixedString_display: partial=%d", partial);
        if (partial) display.display(true);
        else display.display(false);
    }

    void drawMixedString_displayWindow(int x, int y, int w, int h, bool partial)
    {
        ESP_LOGD(TAG, "drawMixedString_displayWindow: x=%d y=%d w=%d h=%d partial=%d", x, y, w, h, partial);
        if (partial)
        {
            display.displayWindow(x, y, w, h);
        }
        else
        {
            // fall back to a full refresh when caller requests non-partial behavior
            display.setPartialWindow(x, y, w, h);
            display.display(false);
        }
    }

    int drawMixedString_width()
    {
        return display.width();
    }

    int drawMixedString_height()
    {
        return display.height();
    }

    void drawMixedString_drawBitmap(int x, int y, const uint8_t* data, int w, int h, int color)
    {
        ESP_LOGD(TAG, "drawMixedString_drawBitmap: x=%d y=%d w=%d h=%d color=%d", x, y, w, h, color);
        display.drawBitmap(x, y, data, w, h, color);
    }

    void drawMixedString_setPartialWindow(int x, int y, int w, int h)
    {
        display.setPartialWindow(x, y, w, h);
    }


    void drawMixedString_firstPage()
    {
        display.firstPage();
    }

    bool drawMixedString_nextPage()
    {
        return display.nextPage();
    }

    void drawMixedString_setCursor(int x, int y)
    {
        display.setCursor(x, y);
    }

    void drawMixedString_print(const char* s)
    {
        display.print(s);
    }

    void drawMixedString_selectFastFullUpdate(bool enable)
    {
        ESP_LOGI(TAG, "drawMixedString_selectFastFullUpdate: enable=%d", enable);
        // call selectFastFullUpdate on the contained epd2 instance (safe straight call)
        display.epd2.selectFastFullUpdate(enable);
    }
}
