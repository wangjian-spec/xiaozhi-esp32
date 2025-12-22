#pragma once
#include <string>

namespace EpdRenderer {
    enum class FontSize : int {
        k12 = 12,
        k16 = 16,
        k24 = 24,
        k32 = 32,
        
    };

    // Wenquanyi BDF font selection (independent from GT30 FontSize).
    // These values are treated as pixel-ish heights for layout (line height, widths).
    enum class FontPt : int {
        k9  = 9,
        k10 = 10,
        k11 = 11,
        k12 = 12,
        k13 = 13,
    };

    void SetBdfFontPt(FontPt pt);
    FontPt GetBdfFontPt();
    // Returns true if native DrawMixedString-based EPD rendering is available
    bool Available();

    // Draw text onto existing buffer (no clear)
    void DrawText(const char* utf8, int x, int y, FontSize font_size = FontSize::k16);
    inline void DrawText(const std::string &utf8, int x, int y, FontSize font_size = FontSize::k16) {
        DrawText(utf8.c_str(), x, y, font_size);
    }
    // Draw a bitmap into the buffer (no refresh)
    void DrawBitmap(const uint8_t* data, int x, int y, int w, int h, int color);

    // BDF binary font support via DrawMixedString wrappers.
    bool BdfLoadFont(const void* data, size_t size);
    bool BdfIsLoaded();
    int DrawBdfGlyph(uint32_t codepoint, int x, int baseline_y, int color);
    // Returns glyph advance in pixels without drawing; returns fallback_advance if missing.
    int BdfGlyphAdvance(uint32_t codepoint, int fallback_advance);
    int DrawBdfText(const char* utf8, int x, int baseline_y, int color);
    int DrawBdfTextN(const char* utf8, size_t utf8_size, int x, int baseline_y, int color);
    // Refresh a specific window on the display
    void DisplayWindow(int x, int y, int w, int h, bool partial = true);
    // Do a partial update of the display (true => partial)
    void Display(bool partial=true);
    // Set the partial window for paged drawing (mirrors GxEPD2::setPartialWindow)
    void setPartialWindow(int x, int y, int w, int h);

    // Clear the whole screen (white)
    void Clear();
    // Initialize EPD hardware (GT30 OCR and display). Safe to call even if EPD not available.
    void Init();
    // Enable or disable selectFastFullUpdate on underlying panel (if supported)
    void SelectFastFullUpdate(bool enable);
    // Paged drawing APIs mirroring GxEPD2:
    void FirstPage();
    bool NextPage();
    void SetCursor(int x, int y);
    void Print(const std::string &s);
}
