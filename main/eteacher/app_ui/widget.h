#pragma once

#include <memory>
#include <vector>
#include "geometry.h"
#include "input.h"
#include "layout_cache.h"
#include "widget_flags.h"

namespace app_ui {

class Painter;

class Widget {
public:
    virtual ~Widget() = default;

    Widget* AddChild(std::unique_ptr<Widget> child);
    Widget* Parent() const;
    const std::vector<std::unique_ptr<Widget>>& Children() const;

    Size Measure(const Size& constraint);
    void Layout(const Rect& rect);

    void Draw(Painter& p);

    virtual bool OnInput(const InputEvent& e) { return false; }

    virtual void OnAttach() {}
    virtual void OnDetach() {}

    void MarkDirty();
    void MarkLayoutDirty();

    void SetVisible(bool v);
    bool Visible() const;

    void SetEnabled(bool v);
    bool Enabled() const;

    void SetFocusable(bool v);

    Rect RectInParent() const;
    Rect RectInWindow() const;
    Rect RectInScreen() const;

    Point MapToGlobal(Point local) const;
    Point MapFromGlobal(Point global) const;

    virtual bool HitTest(Point global) const;

    virtual bool Focusable() const { return flags_.focusable; }
    virtual uint8_t ZOrder() const { return z_order_; }

    Size min_size_{0, 0};
    Size max_size_{INT16_MAX, INT16_MAX};
    Size padding_{0, 0};
    Size margin_{0, 0};

    void SetRectInParent(const Rect& rect);

protected:
    virtual Size OnMeasure(const Size& constraint) = 0;
    virtual void OnLayout(const Rect& rect) {}
    virtual void OnDraw(Painter& p) = 0;

protected:
    Widget* parent_ = nullptr;
    std::vector<std::unique_ptr<Widget>> children_;

    LayoutCache cache_;
    WidgetFlags flags_;
    uint8_t z_order_ = 0;
};

} // namespace app_ui
