#include "ui/epd_renderer.h"
#include <Arduino.h>
#include "esp_log.h"
#include "display.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstddef>
#include <cstdint>

static const char* TAG = "EpdRenderer";

// Use C wrappers from DrawMixedString.cc so this TU doesn't need GxEPD2 headers/macros.
extern "C" void drawMixedString_init();
extern "C" void drawMixedString_fillScreen(int color);
extern "C" void drawMixedString_drawText(const char* utf8, int x, int y, int font_size);
extern "C" void drawMixedString_display(bool partial);
extern "C" void drawMixedString_drawBitmap(int x, int y, const uint8_t* data, int w, int h, int color);
extern "C" bool drawMixedString_bdfLoadFont(const void* data, size_t size);
extern "C" bool drawMixedString_bdfIsLoaded();
extern "C" int drawMixedString_bdfDrawGlyph(uint32_t codepoint, int x, int baseline_y, int color);
extern "C" int drawMixedString_bdfGlyphAdvance(uint32_t codepoint, int fallback_advance);
extern "C" int drawMixedString_bdfDrawUtf8(const char* utf8, int x, int baseline_y, int color);
extern "C" int drawMixedString_bdfDrawUtf8N(const char* utf8, size_t utf8_size, int x, int baseline_y, int color);
extern "C" void drawMixedString_displayWindow(int x, int y, int w, int h, bool partial);
extern "C" int drawMixedString_width();
extern "C" int drawMixedString_height();
extern "C" void drawMixedString_selectFastFullUpdate(bool enable);
extern "C" void drawMixedString_firstPage();
extern "C" bool drawMixedString_nextPage();
extern "C" void drawMixedString_setCursor(int x, int y);
extern "C" void drawMixedString_print(const char* s);
extern "C" void drawMixedString_setPartialWindow(int x, int y, int w, int h);

namespace EpdRenderer {
    static FontPt g_bdf_font_pt = FontPt::k12;

    void SetBdfFontPt(FontPt pt) {
        g_bdf_font_pt = pt;
    }

    FontPt GetBdfFontPt() {
        return g_bdf_font_pt;
    }

    bool Available() {
        return 1;
    }
    void Init() {
        static bool inited = false;
        if (inited) return;
        // Initialize using the centralized wrapper in DrawMixedString.cc. That TU
        // includes GxEPD2 headers and performs SPI/pin initialization.
        ESP_LOGI(TAG, "EpdRenderer::Init() - calling drawMixedString_init");
        drawMixedString_init();
        inited = true;
        ESP_LOGI(TAG, "EpdRenderer::Init() - drawMixedString_init returned; Display dims: w=%d h=%d", drawMixedString_width(), drawMixedString_height());
        // fill white (literal value avoids depending on GxEPD2 macros here)
        drawMixedString_fillScreen(0xFFFF);
    }
    void Clear() {
        drawMixedString_fillScreen(0xFFFF);
    }

    void DrawText(const char* utf8, int x, int y, FontSize font_size) {
        drawMixedString_drawText(utf8, x, y, static_cast<int>(font_size));
    }
    void DrawBitmap(const uint8_t* data, int x, int y, int w, int h, int color) {

            drawMixedString_drawBitmap(x, y, data, w, h, color);
    }

    bool BdfLoadFont(const void* data, size_t size) {
        return drawMixedString_bdfLoadFont(data, size);
    }

    bool BdfIsLoaded() {
        return drawMixedString_bdfIsLoaded();
    }

    int DrawBdfGlyph(uint32_t codepoint, int x, int baseline_y, int color) {
        return drawMixedString_bdfDrawGlyph(codepoint, x, baseline_y, color);
    }

    int BdfGlyphAdvance(uint32_t codepoint, int fallback_advance) {
        return drawMixedString_bdfGlyphAdvance(codepoint, fallback_advance);
    }

    int DrawBdfText(const char* utf8, int x, int baseline_y, int color) {
        return drawMixedString_bdfDrawUtf8(utf8, x, baseline_y, color);
    }

    int DrawBdfTextN(const char* utf8, size_t utf8_size, int x, int baseline_y, int color) {
        return drawMixedString_bdfDrawUtf8N(utf8, utf8_size, x, baseline_y, color);
    }
    void DisplayWindow(int x, int y, int w, int h, bool partial) {
            drawMixedString_displayWindow(x, y, w, h, partial);
    }

    void setPartialWindow(int x, int y, int w, int h) {
            drawMixedString_setPartialWindow(x, y, w, h);
        
    }

    //快速刷新 true 慢速刷新 false
    void Display(bool partial) {
        drawMixedString_display(partial);
    }

    void SelectFastFullUpdate(bool enable) {
        ESP_LOGI(TAG, "EpdRenderer::SelectFastFullUpdate(%d)", enable);
        drawMixedString_selectFastFullUpdate(enable);
    }

    void FirstPage() {

            drawMixedString_firstPage();
        
    }

    bool NextPage() {

      return  drawMixedString_nextPage();
      
    }

    void SetCursor(int x, int y) {

            drawMixedString_setCursor(x, y);
        
    }

    void Print(const std::string &s) {

            drawMixedString_print(s.c_str());
}


}