#include "basic_widgets.h"
#include "painter.h"

#include <algorithm>
#include <cctype>

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool TextHasToken(const std::string& text, const std::string& token) {
    const std::string lower = ToLower(text);
    return lower.find(token) != std::string::npos;
}

bool IsSelectedText(const std::string& text) {
    return TextHasToken(text, "selected") || TextHasToken(text, "active") || TextHasToken(text, "[*]") ||
           TextHasToken(text, "*");
}

} // namespace

namespace app_ui {

void BasicWidget::ApplyStyle(uint16_t style_id) {
    style_id_ = style_id;
    MarkDirty();
}

uint16_t BasicWidget::StyleId() const {
    return style_id_;
}

Size BasicWidget::OnMeasure(const Size& constraint) {
    return constraint;
}

Size ContainerWidget::OnMeasure(const Size& constraint) {
    return constraint;
}

void ContainerWidget::OnDraw(Painter&) {
}

void TextWidget::SetText(const std::string& text) {
    text_ = text;
    MarkDirty();
}

void TextWidget::SetTextId(uint32_t text_id) {
    if (text_id_ == text_id) {
        return;
    }
    text_id_ = text_id;
    MarkDirty();
}

uint32_t TextWidget::TextId() const {
    return text_id_;
}

const std::string& TextWidget::Text() const {
    return text_;
}

Size LabelWidget::OnMeasure(const Size& constraint) {
    return constraint;
}

void LabelWidget::OnDraw(Painter& p) {
    p.DrawText({0, 0}, text_.c_str());
}

void ButtonWidget::OnDraw(Painter& p) {
    Rect parent = RectInParent();
    Rect rect{0, 0, parent.w, parent.h};
    p.SetDrawColor(Color::Black);
    p.SetTextColor(Color::Black);
    p.DrawRect(rect);
    p.DrawText({2, 2}, text_.c_str());
}

void ImageWidget::OnDraw(Painter& p) {
    Rect parent = RectInParent();
    Rect rect{0, 0, parent.w, parent.h};
    p.SetDrawColor(Color::Black);
    p.DrawRect(rect);
    if (!text_.empty()) {
        p.DrawText({2, 2}, text_.c_str());
    }
}

void SeparatorWidget::OnDraw(Painter& p) {
    Rect parent = RectInParent();
    Rect rect{0, 0, parent.w, parent.h};
    p.SetDrawColor(Color::Black);
    const int y = rect.h > 0 ? rect.h / 2 : 0;
    p.DrawHLine({0, static_cast<int16_t>(y)}, rect.w);
}

void CheckboxWidget::SetChecked(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    MarkDirty();
}

bool CheckboxWidget::Checked() const {
    return checked_;
}

void CheckboxWidget::OnDraw(Painter& p) {
    Rect parent = RectInParent();
    Rect rect{0, 0, parent.w, parent.h};
    p.SetDrawColor(Color::Black);
    p.SetTextColor(Color::Black);
    const int box = std::min(12, rect.h > 0 ? rect.h : 12);
    p.DrawRect({0, 0, static_cast<int16_t>(box), static_cast<int16_t>(box)});
    if (checked_) {
        p.DrawText({2, static_cast<int16_t>(box - 2)}, "✓");
    }
    p.DrawText({static_cast<int16_t>(box + 4), 0}, text_.c_str());
}

void RadioWidget::SetChecked(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    MarkDirty();
}

bool RadioWidget::Checked() const {
    return checked_;
}

void RadioWidget::OnDraw(Painter& p) {
    Rect parent = RectInParent();
    Rect rect{0, 0, parent.w, parent.h};
    const int radius = std::min(5, rect.h / 2);
    p.SetDrawColor(Color::Black);
    p.SetTextColor(Color::Black);
    p.DrawCircle({static_cast<int16_t>(radius + 1), static_cast<int16_t>(radius + 1)}, radius);
    if (checked_) {
        p.FillRect({static_cast<int16_t>(radius - 1), static_cast<int16_t>(radius - 1), 4, 4});
    }
    p.DrawText({static_cast<int16_t>(radius * 2 + 4), 0}, text_.c_str());
}

void SwitchWidget::SetChecked(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    MarkDirty();
}

bool SwitchWidget::Checked() const {
    return checked_;
}

void SwitchWidget::OnDraw(Painter& p) {
    Rect parent = RectInParent();
    Rect rect{0, 0, parent.w, parent.h};
    const int box_w = 28;
    const int box_h = std::min(12, rect.h > 0 ? rect.h : 12);
    const bool is_on = checked_;

    p.SetDrawColor(Color::Black);
    p.DrawRect({0, 0, static_cast<int16_t>(box_w), static_cast<int16_t>(box_h)});
    if (is_on) {
        p.InvertRect({1, 1, static_cast<int16_t>(box_w - 2), static_cast<int16_t>(box_h - 2)});
        p.SetTextColor(Color::White);
        p.DrawText({2, 0}, "ON");
        p.SetTextColor(Color::Black);
    } else {
        p.DrawText({2, 0}, "OFF");
    }
    p.DrawText({static_cast<int16_t>(box_w + 4), 0}, text_.c_str());
}

void ProgressWidget::OnDraw(Painter& p) {
    Rect parent = RectInParent();
    Rect rect{0, 0, parent.w, parent.h};
    const int percent = value_ > 100 ? 100 : value_;
    p.SetDrawColor(Color::Black);
    p.DrawRect(rect);
    if (rect.w > 2 && rect.h > 2 && percent > 0) {
        const int fill_w = (rect.w - 2) * percent / 100;
        p.FillRect({1, 1, static_cast<int16_t>(fill_w), static_cast<int16_t>(rect.h - 2)});
    }
}

void ProgressWidget::SetValue(uint8_t value) {
    const uint8_t clamped = value > 100 ? 100 : value;
    if (value_ == clamped) {
        return;
    }
    value_ = clamped;
    MarkDirty();
}

uint8_t ProgressWidget::Value() const {
    return value_;
}

void MenuItemWidget::OnDraw(Painter& p) {
    Rect parent = RectInParent();
    Rect rect{0, 0, parent.w, parent.h};
    const bool selected = IsSelectedText(text_);
    p.SetDrawColor(Color::Black);
    if (selected) {
        p.InvertRect(rect);
        p.SetTextColor(Color::White);
    } else {
        p.SetTextColor(Color::Black);
    }
    p.DrawText({2, 0}, text_.c_str());
    p.SetTextColor(Color::Black);
    p.DrawHLine({0, static_cast<int16_t>(rect.h - 1)}, rect.w);
}

void TabItemWidget::OnDraw(Painter& p) {
    Rect parent = RectInParent();
    Rect rect{0, 0, parent.w, parent.h};
    const bool selected = IsSelectedText(text_);
    p.SetDrawColor(Color::Black);
    p.SetTextColor(Color::Black);
    p.DrawText({2, 0}, text_.c_str());
    if (selected) {
        p.FillRect({0, static_cast<int16_t>(rect.h - 2), rect.w, 2});
    }
}

} // namespace app_ui
