#include "widget_internal.h"

#include <algorithm>

namespace app_ui {
using namespace widget_internal;

void FrameWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    p.SetTextColor(Color::Black);
    if (profile_.draw_border) {
        p.SetDrawColor(Color::Black);
        p.DrawRect(rect);
    }
    if (!text_.empty()) {
        const int16_t x = profile_.text_offset_x;
        const int16_t top = profile_.text_offset_y;
        const Size line_size = p.MeasureText("A", nullptr);
        int16_t line_h = line_size.h > 0 ? line_size.h : 14;
        if (line_h < profile_.min_line_height) {
            line_h = profile_.min_line_height;
        }

        size_t start = 0;
        int line_index = 0;
        while (start <= text_.size()) {
            const size_t pos = text_.find('\n', start);
            const size_t end = (pos == std::string::npos) ? text_.size() : pos;
            const int16_t y = static_cast<int16_t>(top + line_index * line_h);
            if (y + line_h > rect.h - 1) {
                break;
            }

            const std::string line = text_.substr(start, end - start);
            if (!line.empty()) {
                p.DrawText({x, y}, line.c_str());
            }

            if (pos == std::string::npos) {
                break;
            }
            start = pos + 1;
            ++line_index;
        }
    }
}

void FrameWidget::SetProfile(const FrameProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const FrameProfile& FrameWidget::Profile() const {
    return profile_;
}

void MenuWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    p.SetTextColor(Color::Black);
    p.SetDrawColor(Color::Black);
    if (profile_.draw_border) {
        p.DrawRect(rect);
    }
    const int divider_count = std::max(0, profile_.divider_count);
    if (divider_count > 0 && rect.h > 0) {
        const int16_t margin = std::max<int16_t>(0, profile_.divider_margin_x);
        const int16_t line_w = std::max<int16_t>(0, static_cast<int16_t>(rect.w - margin * 2));
        for (int i = 1; i <= divider_count; ++i) {
            const int y = rect.h * i / (divider_count + 1);
            p.DrawHLine({margin, static_cast<int16_t>(y)}, line_w);
        }
    }
    if (!text_.empty()) {
        p.DrawText({profile_.text_offset_x, profile_.text_offset_y}, text_.c_str());
    }
}

void MenuWidget::SetProfile(const MenuProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const MenuProfile& MenuWidget::Profile() const {
    return profile_;
}

InputResult DialogWidget::OnInput(const InputEvent& e, InputPhase phase) {
    DialogBehavior* behavior = behavior_ ? behavior_.get() : &DefaultDialogBehaviorInstance();
    return behavior->OnInput(*this, e, phase);
}

void DialogWidget::SetProfile(const DialogProfile& profile) {
    profile_ = profile;
    ClampSelection();
    MarkMeasureDirty();
    MarkDirty();
}

const DialogProfile& DialogWidget::Profile() const {
    return profile_;
}

void DialogWidget::SetBehavior(std::unique_ptr<DialogBehavior> behavior) {
    behavior_ = std::move(behavior);
    MarkMeasureDirty();
    MarkDirty();
}

void DialogWidget::ResetBehavior() {
    behavior_.reset();
    MarkMeasureDirty();
    MarkDirty();
}

void DialogWidget::SetItems(std::vector<std::string> items) {
    items_ = std::move(items);
    items_from_text_cached_ = true;
    ClampSelection();
    MarkDirty();
}

const std::vector<std::string>& DialogWidget::Items() {
    EnsureItemsFromText();
    return items_;
}

int DialogWidget::SelectedIndex() const {
    return selected_index_;
}

void DialogWidget::SetSelectedIndex(int index) {
    selected_index_ = index;
    ClampSelection();
    MarkDirty();
}

void DialogWidget::NotifySelected() {
    ClampSelection();
    if (on_selected_) {
        on_selected_(this, selected_index_, on_selected_ctx_);
    }
}

void DialogWidget::SetOnSelected(SelectCallback callback, void* ctx) {
    on_selected_ = callback;
    on_selected_ctx_ = ctx;
}

void DialogWidget::EnsureItemsFromText() {
    if (items_from_text_cached_) {
        return;
    }
    items_.clear();

    items_ = ParseItemsFromText(text_);
    items_from_text_cached_ = true;
}

void DialogWidget::ClampSelection() {
    if (profile_.mode == DialogProfile::Mode::Prompt) {
        selected_index_ = ClampIndexByCount(selected_index_, 2);
        return;
    }

    EnsureItemsFromText();
    selected_index_ = ClampIndexByCount(selected_index_, static_cast<int>(items_.size()));
}

void DialogWidget::OnTextChanged() {
    items_from_text_cached_ = false;
    items_.clear();
    ClampSelection();
    MarkDirty();
}

void DialogWidget::OnDraw(Painter& p) {
    DialogBehavior* behavior = behavior_ ? behavior_.get() : &DefaultDialogBehaviorInstance();
    behavior->OnDraw(*this, p);
}

} // namespace app_ui
