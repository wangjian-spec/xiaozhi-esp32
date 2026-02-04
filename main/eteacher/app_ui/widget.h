#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "types.h"

namespace app_ui {

struct InputEvent;
class Painter;

struct LayoutCache {
    Size measured{};
    Rect layout{};
    bool measure_valid = false;
    bool layout_valid = false;
    uint32_t measure_version = 0;
    uint32_t layout_version = 0;
    Size last_constraint{};
};

struct WidgetFlags {
    uint8_t visible : 1;
    uint8_t enabled : 1;
    uint8_t focusable : 1;
    uint8_t focused : 1;
    uint8_t dirty : 1;
    uint8_t layout_dirty : 1;

    WidgetFlags()
        : visible(1),
          enabled(1),
          focusable(0),
          focused(0),
          dirty(1),
          layout_dirty(1) {}
};

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

class BasicWidget : public Widget {
public:
    void ApplyStyle(uint16_t style_id);
    uint16_t StyleId() const;

protected:
    Size OnMeasure(const Size& constraint) override;

protected:
    uint16_t style_id_ = kInvalidStyleId;
};

class ContainerWidget : public BasicWidget {
protected:
    Size OnMeasure(const Size& constraint) override;
    void OnDraw(Painter& p) override;
};

class TextWidget : public BasicWidget {
public:
    void SetText(const std::string& text);
    const std::string& Text() const;
    void SetTextId(uint32_t text_id);
    uint32_t TextId() const;

protected:
    std::string text_;
    uint32_t text_id_ = 0;
};

class LabelWidget : public TextWidget {
protected:
    Size OnMeasure(const Size& constraint) override;
    void OnDraw(Painter& p) override;
};

class ButtonWidget : public LabelWidget {
protected:
    void OnDraw(Painter& p) override;
};

class ImageWidget : public TextWidget {
protected:
    void OnDraw(Painter& p) override;
};

class SeparatorWidget : public BasicWidget {
protected:
    void OnDraw(Painter& p) override;
};

class CheckboxWidget : public TextWidget {
public:
    void SetChecked(bool checked);
    bool Checked() const;

protected:
    void OnDraw(Painter& p) override;

private:
    bool checked_ = false;
};

class RadioWidget : public TextWidget {
public:
    void SetChecked(bool checked);
    bool Checked() const;

protected:
    void OnDraw(Painter& p) override;

private:
    bool checked_ = false;
};

class SwitchWidget : public TextWidget {
public:
    void SetChecked(bool checked);
    bool Checked() const;

protected:
    void OnDraw(Painter& p) override;

private:
    bool checked_ = false;
};

class ProgressWidget : public TextWidget {
public:
    void SetValue(uint8_t value);
    uint8_t Value() const;

protected:
    void OnDraw(Painter& p) override;

private:
    uint8_t value_ = 0;
};

class MenuItemWidget : public TextWidget {
protected:
    void OnDraw(Painter& p) override;
};

class TabItemWidget : public TextWidget {
protected:
    void OnDraw(Painter& p) override;
};

} // namespace app_ui
