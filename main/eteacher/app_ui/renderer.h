#pragma once

#include <vector>

#include "types.h"

// 渲染器接口定义
// 本头文件定义绘制器、脏区域跟踪及渲染列表等接口，供 UI 引擎进行绘制。

class CustomEpdDisplay;
class Adafruit_GFX;

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

class EpdPainter : public Painter {
public:
    EpdPainter(::CustomEpdDisplay* epd, ::Adafruit_GFX& gfx);

    ::CustomEpdDisplay* Epd() { return epd_; }
    ::Adafruit_GFX& Gfx() { return gfx_; }
    Point Offset() const { return offset_; }

    void SetClip(const Rect&) override;
    void PushClip(const Rect&) override;
    void PopClip() override;
    void DrawText(Point, const char*) override;
    Size MeasureText(const char*, Font*) override;
    void DrawRect(const Rect&) override;
    void FillRect(const Rect&) override;
    void DrawImage(Point, const Image*) override;
    void SetFont(Font*) override;
    void SetDrawColor(Color) override;
    void SetTextColor(Color) override;
    void DrawCircle(Point center, int radius) override;
    void SetTransform(const Point& offset) override;
    void SetAlpha(float alpha) override;

private:
    ::CustomEpdDisplay* epd_ = nullptr;
    ::Adafruit_GFX& gfx_;
    Point offset_{};
    uint16_t draw_color_ = 0;
    uint16_t text_color_ = 0;
};

enum class DirtyReason : uint8_t {
    Visual,
    Layout,
    Full
};

struct DirtyItem {
    Rect rect;
    DirtyReason reason = DirtyReason::Visual;
};

class DirtyTracker {
public:
    void Add(const Rect& rect, DirtyReason reason);
    bool HasDirty() const;
    Rect Merge() const;
    bool RequireFullRefresh() const;
    bool Intersects(const Rect& rect) const;
    const std::vector<DirtyItem>& Items() const;
    void Clear();
    void SetFullRect(const Rect& rect);

private:
    std::vector<DirtyItem> dirty_;
    Rect full_rect_{};
};

class Widget;

class LayoutEngine {
public:
    void LayoutTree(Widget* root, const Rect& area);

private:
    void LayoutRecursive(Widget* node, const Rect& area);
};

struct RenderCapabilities {
    bool partial_refresh = true;
    bool grayscale = false;
    bool invert = false;
    bool double_buffer = false;
};

struct RenderObject {
    Rect rect;
    Widget* widget = nullptr;
    uint8_t z = 0;
    uint32_t depth = 0;
    float alpha = 1.0f;
    uint32_t order = 0;
};

class RenderList {
public:
    void Clear();
    void Build(Widget* root);
    const std::vector<RenderObject>& Items() const;

private:
    void Traverse(Widget* node, uint32_t depth, uint32_t& order);

    std::vector<RenderObject> items_;
};

class Renderer {
public:
    const RenderCapabilities& Capabilities() const;
    void Render(RenderList& list, DirtyTracker& dirty, Painter& painter);

private:
    RenderCapabilities caps_{};
};

} // namespace app_ui
