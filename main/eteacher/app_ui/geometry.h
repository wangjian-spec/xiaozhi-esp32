#pragma once

#include <cstdint>

namespace app_ui {

struct Size {
    int16_t w = 0;
    int16_t h = 0;

    bool operator==(const Size& other) const {
        return w == other.w && h == other.h;
    }

    bool operator!=(const Size& other) const {
        return !(*this == other);
    }
};

struct Point {
    int16_t x = 0;
    int16_t y = 0;
};

struct Rect {
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;

    bool IsEmpty() const {
        return w <= 0 || h <= 0;
    }

    bool Contains(Point p) const {
        return p.x >= x && p.y >= y && p.x < (x + w) && p.y < (y + h);
    }

    bool Intersects(const Rect& other) const {
        return !(x + w <= other.x || other.x + other.w <= x ||
                 y + h <= other.y || other.y + other.h <= y);
    }

    Rect Union(const Rect& other) const {
        if (IsEmpty()) {
            return other;
        }
        if (other.IsEmpty()) {
            return *this;
        }
        const int16_t x1 = (x < other.x) ? x : other.x;
        const int16_t y1 = (y < other.y) ? y : other.y;
        const int16_t x2 = ((x + w) > (other.x + other.w)) ? (x + w) : (other.x + other.w);
        const int16_t y2 = ((y + h) > (other.y + other.h)) ? (y + h) : (other.y + other.h);
        return {x1, y1, static_cast<int16_t>(x2 - x1), static_cast<int16_t>(y2 - y1)};
    }

    Rect Offset(Point p) const {
        return {static_cast<int16_t>(x + p.x), static_cast<int16_t>(y + p.y), w, h};
    }
};

} // namespace app_ui
