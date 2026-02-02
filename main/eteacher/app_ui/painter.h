#pragma once

#include "geometry.h"

namespace app_ui {

struct Font {};
struct Image {};

enum class Color : uint8_t {
    Black = 0,
    White = 1,
};

class Painter {
public:
    virtual ~Painter() = default;

    virtual void SetClip(const Rect&) = 0;
    virtual void PushClip(const Rect&) = 0;
    virtual void PopClip() = 0;
    virtual void DrawText(Point, const char*) = 0;
    virtual Size MeasureText(const char*, Font*) = 0;
    virtual void DrawRect(const Rect&) = 0;
    virtual void FillRect(const Rect&) = 0;
    virtual void DrawImage(Point, const Image*) = 0;
    virtual void SetFont(Font*) = 0;
    virtual void SetDrawColor(Color) = 0;
    virtual void SetTextColor(Color) = 0;
    virtual void DrawCircle(Point center, int radius) = 0;
    virtual void SetTransform(const Point& offset) = 0;
    virtual void SetAlpha(float alpha) = 0;

    virtual void DrawHLine(Point p, int w) {
        FillRect({p.x, p.y, static_cast<int16_t>(w), 1});
    }

    virtual void DrawVLine(Point p, int h) {
        FillRect({p.x, p.y, 1, static_cast<int16_t>(h)});
    }

    virtual void InvertRect(const Rect& rect) {
        FillRect(rect);
    }
};

} // namespace app_ui
