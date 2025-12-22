#include "esp_log.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include<driver_gt30l32s4w.h>
#include<driver_gt30l32s4w_basic.h>
#include<driver_gt30l32s4w_interface.h>
#include<utf8_to_gb2312_table.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include "DrawMixedString.h"

namespace {

#pragma pack(push, 1)
struct BdfFontHeaderBin {
    char magic[4];      // "BDFB"
    uint8_t version;
    uint16_t ascent;
    uint16_t descent;
    uint32_t glyph_count;
    int16_t bbox_w, bbox_h, bbox_x, bbox_y;
};

struct BdfGlyphEntryBin {
    uint32_t codepoint;
    uint16_t width;
    uint16_t height;
    int16_t x_offset;
    int16_t y_offset;
    uint16_t advance;
    uint32_t bitmap_offset; // bytes from bitmap base
};
#pragma pack(pop)

static uint16_t ReadLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
static int16_t ReadLE16S(const uint8_t* p) {
    return static_cast<int16_t>(ReadLE16(p));
}
static uint32_t ReadLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] |
                                (static_cast<uint32_t>(p[1]) << 8) |
                                (static_cast<uint32_t>(p[2]) << 16) |
                                (static_cast<uint32_t>(p[3]) << 24));
}

static uint16_t ReadBE16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
static int16_t ReadBE16S(const uint8_t* p) {
    return static_cast<int16_t>(ReadBE16(p));
}
static uint32_t ReadBE32(const uint8_t* p) {
    return static_cast<uint32_t>((static_cast<uint32_t>(p[0]) << 24) |
                                (static_cast<uint32_t>(p[1]) << 16) |
                                (static_cast<uint32_t>(p[2]) << 8) |
                                 static_cast<uint32_t>(p[3]));
}

static size_t BitmapBytesFor(uint16_t w, uint16_t h) {
    const size_t row_bytes = (static_cast<size_t>(w) + 7u) / 8u;
    return row_bytes * static_cast<size_t>(h);
}

class BdfFontBin {
public:
    bool Load(const void* data, size_t size) {
        data_ = nullptr;
        size_ = 0;
        entries_ = nullptr;
        bitmap_base_ = nullptr;
        bitmap_size_ = 0;
        glyph_count_ = 0;
        if (!data || size < sizeof(BdfFontHeaderBin)) {
            return false;
        }
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        if (memcmp(bytes, "BDFB", 4) != 0) {
            return false;
        }

        // Auto-detect endianness: some generators write big-endian numeric fields.
        const uint32_t glyph_count_le = ReadLE32(bytes + 9);
        const uint32_t glyph_count_be = ReadBE32(bytes + 9);

        const uint64_t table_bytes_le = static_cast<uint64_t>(sizeof(BdfFontHeaderBin)) +
                                        static_cast<uint64_t>(glyph_count_le) * static_cast<uint64_t>(sizeof(BdfGlyphEntryBin));
        const uint64_t table_bytes_be = static_cast<uint64_t>(sizeof(BdfFontHeaderBin)) +
                                        static_cast<uint64_t>(glyph_count_be) * static_cast<uint64_t>(sizeof(BdfGlyphEntryBin));

        // Plausibility checks: table must fit, glyph count must be non-zero.
        const bool le_ok = (glyph_count_le > 0) && (table_bytes_le <= size);
        const bool be_ok = (glyph_count_be > 0) && (table_bytes_be <= size);

        if (!le_ok && !be_ok) {
            return false;
        }

        big_endian_ = (!le_ok && be_ok);
        glyph_count_ = big_endian_ ? glyph_count_be : glyph_count_le;

        const uint64_t table_bytes = big_endian_ ? table_bytes_be : table_bytes_le;
        entries_ = bytes + sizeof(BdfFontHeaderBin);
        bitmap_base_ = bytes + static_cast<size_t>(table_bytes);
        bitmap_size_ = size - static_cast<size_t>(table_bytes);

        // validate sorted entries and bitmap bounds
        uint32_t prev_cp = 0;
        bool have_prev = false;
        for (uint32_t i = 0; i < glyph_count_; ++i) {
            const uint8_t* ep = entries_ + static_cast<size_t>(i) * sizeof(BdfGlyphEntryBin);
            const uint32_t cp = big_endian_ ? ReadBE32(ep + 0) : ReadLE32(ep + 0);
            const uint16_t w = big_endian_ ? ReadBE16(ep + 4) : ReadLE16(ep + 4);
            const uint16_t h = big_endian_ ? ReadBE16(ep + 6) : ReadLE16(ep + 6);
            const uint32_t off = big_endian_ ? ReadBE32(ep + 14) : ReadLE32(ep + 14);
            if (have_prev && cp <= prev_cp) {
                return false;
            }
            have_prev = true;
            prev_cp = cp;
            const size_t bytes_needed = BitmapBytesFor(w, h);
            if (static_cast<uint64_t>(off) + static_cast<uint64_t>(bytes_needed) > bitmap_size_) {
                return false;
            }
        }

        data_ = bytes;
        size_ = size;

        return true;
    }

    bool IsLoaded() const { return data_ != nullptr; }

    bool FindGlyph(uint32_t codepoint, BdfGlyphEntryBin& out) const {
        if (!IsLoaded() || glyph_count_ == 0) {
            return false;
        }
        size_t lo = 0;
        size_t hi = static_cast<size_t>(glyph_count_);
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            const uint8_t* ep = entries_ + mid * sizeof(BdfGlyphEntryBin);
            const uint32_t mid_cp = big_endian_ ? ReadBE32(ep + 0) : ReadLE32(ep + 0);
            if (mid_cp == codepoint) {
                out.codepoint = mid_cp;
                out.width = big_endian_ ? ReadBE16(ep + 4) : ReadLE16(ep + 4);
                out.height = big_endian_ ? ReadBE16(ep + 6) : ReadLE16(ep + 6);
                out.x_offset = big_endian_ ? ReadBE16S(ep + 8) : ReadLE16S(ep + 8);
                out.y_offset = big_endian_ ? ReadBE16S(ep + 10) : ReadLE16S(ep + 10);
                out.advance = big_endian_ ? ReadBE16(ep + 12) : ReadLE16(ep + 12);
                out.bitmap_offset = big_endian_ ? ReadBE32(ep + 14) : ReadLE32(ep + 14);
                return true;
            }
            if (mid_cp < codepoint) lo = mid + 1;
            else hi = mid;
        }
        return false;
    }

    const uint8_t* BitmapPtr(const BdfGlyphEntryBin& g) const {
        if (!IsLoaded()) return nullptr;
        const size_t need = BitmapBytesFor(g.width, g.height);
        const uint64_t end = static_cast<uint64_t>(g.bitmap_offset) + static_cast<uint64_t>(need);
        if (end > bitmap_size_) return nullptr;
        return bitmap_base_ + g.bitmap_offset;
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    const uint8_t* entries_ = nullptr;
    const uint8_t* bitmap_base_ = nullptr;
    size_t bitmap_size_ = 0;
    uint32_t glyph_count_ = 0;
    bool big_endian_ = false;
};

static BdfFontBin g_bdf_font;

static uint32_t DecodeUtf8Simple(const char* buf, size_t size, size_t& i) {
    if (i >= size) return 0;
    const unsigned char c0 = static_cast<unsigned char>(buf[i]);
    if (c0 < 0x80) { i += 1; return c0; }
    if ((c0 >> 5) == 0x6 && i + 1 < size) {
        const unsigned char c1 = static_cast<unsigned char>(buf[i + 1]);
        if ((c1 & 0xC0) != 0x80) { i += 1; return c0; }
        const uint32_t cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
        i += 2;
        return cp;
    }
    if ((c0 >> 4) == 0xE && i + 2 < size) {
        const unsigned char c1 = static_cast<unsigned char>(buf[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(buf[i + 2]);
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80)) { i += 1; return c0; }
        const uint32_t cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        i += 3;
        return cp;
    }
    if ((c0 >> 3) == 0x1E && i + 3 < size) {
        const unsigned char c1 = static_cast<unsigned char>(buf[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(buf[i + 2]);
        const unsigned char c3 = static_cast<unsigned char>(buf[i + 3]);
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80)) { i += 1; return c0; }
        const uint32_t cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        i += 4;
        return cp;
    }
    i += 1;
    return c0;
}

static int DrawBdfGlyphInternal(uint32_t codepoint, int x, int baseline_y, int color) {
    if (!g_bdf_font.IsLoaded()) {
        return 0;
    }
    BdfGlyphEntryBin g{};
    if (!g_bdf_font.FindGlyph(codepoint, g)) {
        return 0;
    }
    const uint8_t* bmp = g_bdf_font.BitmapPtr(g);
    if (!bmp) {
        return 0;
    }

    const int draw_x = x + static_cast<int>(g.x_offset);
    const int draw_y = baseline_y - (static_cast<int>(g.y_offset) + static_cast<int>(g.height));
    display.drawBitmap(draw_x, draw_y, bmp, static_cast<int>(g.width), static_cast<int>(g.height), color);
    return static_cast<int>(g.advance);
}

} // namespace

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

    bool drawMixedString_bdfLoadFont(const void* data, size_t size)
    {
        const bool ok = g_bdf_font.Load(data, size);
        if (!ok) {
            ESP_LOGW(TAG, "BDF font load failed");
        }
        return ok;
    }

    bool drawMixedString_bdfIsLoaded()
    {
        return g_bdf_font.IsLoaded();
    }

    int drawMixedString_bdfDrawGlyph(uint32_t codepoint, int x, int baseline_y, int color)
    {
        return DrawBdfGlyphInternal(codepoint, x, baseline_y, color);
    }

    int drawMixedString_bdfGlyphAdvance(uint32_t codepoint, int fallback_advance)
    {
        if (!g_bdf_font.IsLoaded()) {
            return fallback_advance;
        }
        BdfGlyphEntryBin g{};
        if (!g_bdf_font.FindGlyph(codepoint, g)) {
            return fallback_advance;
        }
        return static_cast<int>(g.advance);
    }

    int drawMixedString_bdfDrawUtf8N(const char* utf8, size_t utf8_size, int x, int baseline_y, int color)
    {
        if (!utf8 || !g_bdf_font.IsLoaded()) {
            return x;
        }
        size_t i = 0;
        while (i < utf8_size) {
            const uint32_t cp = DecodeUtf8Simple(utf8, utf8_size, i);
            if (cp == 0) {
                break;
            }
            if (cp == '\n' || cp == '\r') {
                continue;
            }
            x += DrawBdfGlyphInternal(cp, x, baseline_y, color);
        }
        return x;
    }

    int drawMixedString_bdfDrawUtf8(const char* utf8, int x, int baseline_y, int color)
    {
        if (!utf8) {
            return x;
        }
        return drawMixedString_bdfDrawUtf8N(utf8, strlen(utf8), x, baseline_y, color);
    }
}
