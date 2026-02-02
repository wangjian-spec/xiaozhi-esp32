#pragma once

#include <string>
#include "ui_layout_types.h"
#include "widget.h"

namespace app_ui {

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
