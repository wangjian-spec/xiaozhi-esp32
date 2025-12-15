#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ui/bdf_font.h"

// Notes:
// - Assumes glyph bitmap data is 1-bit-per-pixel, row-major, MSB-first per byte,
//   which matches what GxEPD2::drawBitmap expects for monochrome bitmaps.
// - Coordinates here treat (x, baseline_y) as the glyph origin on the baseline.
//   With BDF metrics, BBX y_offset is the offset from baseline to the *lower* edge.
//   Therefore top-left y for drawBitmap is: baseline_y - (y_offset + height).

namespace bdf_font {

// Minimal UTF-8 decoder (no allocation). Advances i by 1..4.
// Returns 0 on end-of-buffer.
inline uint32_t DecodeUtf8(const char* buf, size_t size, size_t& i) {
    if (i >= size) {
        return 0;
    }
    const unsigned char c0 = static_cast<unsigned char>(buf[i]);
    if (c0 < 0x80) {
        i += 1;
        return c0;
    }
    if ((c0 >> 5) == 0x6 && i + 1 < size) {
        const unsigned char c1 = static_cast<unsigned char>(buf[i + 1]);
        if ((c1 & 0xC0) != 0x80) {
            i += 1;
            return c0;
        }
        const uint32_t cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
        i += 2;
        return cp;
    }
    if ((c0 >> 4) == 0xE && i + 2 < size) {
        const unsigned char c1 = static_cast<unsigned char>(buf[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(buf[i + 2]);
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80)) {
            i += 1;
            return c0;
        }
        const uint32_t cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        i += 3;
        return cp;
    }
    if ((c0 >> 3) == 0x1E && i + 3 < size) {
        const unsigned char c1 = static_cast<unsigned char>(buf[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(buf[i + 2]);
        const unsigned char c3 = static_cast<unsigned char>(buf[i + 3]);
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80)) {
            i += 1;
            return c0;
        }
        const uint32_t cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        i += 4;
        return cp;
    }

    // Invalid lead byte; consume one.
    i += 1;
    return c0;
}

// Render a single glyph at (x, baseline_y). Returns the advance (pen movement in X).
// If glyph missing, returns 0.
template <typename DisplayT>
inline int16_t DrawGlyph(DisplayT& display,
                         const BdfFont& font,
                         uint32_t codepoint,
                         int16_t x,
                         int16_t baseline_y,
                         uint16_t color) {
    BdfGlyphEntry g{};
    const uint8_t* bmp = nullptr;
    if (!font.GetGlyph(codepoint, g, bmp)) {
        return 0;
    }

    const int16_t draw_x = static_cast<int16_t>(x + g.x_offset);
    const int16_t draw_y = static_cast<int16_t>(baseline_y - (g.y_offset + static_cast<int16_t>(g.height)));

    // GxEPD2 drawBitmap expects top-left coordinates.
    display.drawBitmap(draw_x, draw_y, bmp, static_cast<int16_t>(g.width), static_cast<int16_t>(g.height), color);

    return static_cast<int16_t>(g.advance);
}

// Draw a UTF-8 string left-to-right. Returns final X after drawing.
// Overload with explicit size avoids strlen.
template <typename DisplayT>
inline int16_t DrawUtf8(DisplayT& display,
                        const BdfFont& font,
                        const char* utf8,
                        size_t utf8_size,
                        int16_t x,
                        int16_t baseline_y,
                        uint16_t color) {
    size_t i = 0;
    while (i < utf8_size) {
        const uint32_t cp = DecodeUtf8(utf8, utf8_size, i);
        if (cp == 0) {
            break;
        }
        if (cp == '\n' || cp == '\r') {
            continue;
        }
        x = static_cast<int16_t>(x + DrawGlyph(display, font, cp, x, baseline_y, color));
    }
    return x;
}

template <typename DisplayT>
inline int16_t DrawUtf8(DisplayT& display,
                        const BdfFont& font,
                        const char* utf8,
                        int16_t x,
                        int16_t baseline_y,
                        uint16_t color) {
    if (!utf8) {
        return x;
    }
    return DrawUtf8(display, font, utf8, std::strlen(utf8), x, baseline_y, color);
}

} // namespace bdf_font
