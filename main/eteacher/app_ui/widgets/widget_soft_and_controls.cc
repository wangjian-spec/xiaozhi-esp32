#include "widget_internal.h"

#include "../input.h"
#include "eteacher/app_ui/status_bar.h"
#include "boards/common/board.h"

#include <algorithm>
#include <cstring>

namespace app_ui {
using namespace widget_internal;

SoftKeyboardWidget::SoftKeyboardWidget() {
    SetFocusable(true);
    SetFontName("wenquanyi_11pt");
}

void SoftKeyboardWidget::SetProfile(const SoftKeyboardProfile& profile) {
    profile_ = profile;
    const int pages = ResolveKeyboardPages(profile_);
    page_ = (profile_.page < 0) ? 0 : (profile_.page >= pages ? (pages - 1) : profile_.page);
    selected_index_ = profile_.selected_index;
    ClampSelection();
    MarkMeasureDirty();
    MarkDirty();
}

const SoftKeyboardProfile& SoftKeyboardWidget::Profile() const {
    return profile_;
}

InputResult SoftKeyboardWidget::OnInput(const InputEvent& e, InputPhase phase) {
    if (phase == InputPhase::Capture) {
        return InputResult::Continue;
    }
    if (!Enabled() || !Visible()) {
        return InputResult::Continue;
    }
    if (!IsKeyDownOrRepeat(e)) {
        return InputResult::Continue;
    }

    const KeyCode key = static_cast<KeyCode>(e.key);
    if (key == KeyCode::Up || key == KeyCode::Down || key == KeyCode::Left || key == KeyCode::Right) {
        const int rows = ResolveKeyboardRows(profile_);
        const int cols = ResolveKeyboardCols(profile_);
        const bool is_repeat = (e.type == InputType::KeyRepeat);
        const int key_value = static_cast<int>(key);
        uint32_t now_ms = e.timestamp;
        if (is_repeat) {
            const uint32_t repeat_step_ms = profile_.nav_repeat_step_ms > 0 ? profile_.nav_repeat_step_ms : 60;
            if (now_ms == 0) {
                now_ms = last_nav_repeat_ms_ + repeat_step_ms;
            }
            if (last_nav_key_ == key_value && last_nav_repeat_ms_ != 0 && now_ms > last_nav_repeat_ms_ &&
                (now_ms - last_nav_repeat_ms_) < repeat_step_ms) {
                return InputResult::Consume;
            }
            last_nav_repeat_ms_ = now_ms;
        } else {
            last_nav_repeat_ms_ = now_ms;
        }
        last_nav_key_ = key_value;

        int row = selected_index_ / cols;
        int col = selected_index_ % cols;
        if (profile_.wrap_navigation) {
            if (key == KeyCode::Up) {
                row = (row + rows - 1) % rows;
            } else if (key == KeyCode::Down) {
                row = (row + 1) % rows;
            } else if (key == KeyCode::Left) {
                col = (col + cols - 1) % cols;
            } else if (key == KeyCode::Right) {
                col = (col + 1) % cols;
            }
        } else {
            if (key == KeyCode::Up) {
                row = std::max(0, row - 1);
            } else if (key == KeyCode::Down) {
                row = std::min(rows - 1, row + 1);
            } else if (key == KeyCode::Left) {
                col = std::max(0, col - 1);
            } else if (key == KeyCode::Right) {
                col = std::min(cols - 1, col + 1);
            }
        }
        const int next_index = row * cols + col;
        if (next_index != selected_index_) {
            selected_index_ = next_index;
            MarkDirty();
        }
        return InputResult::Consume;
    }

    if (profile_.activation_enabled && key == profile_.activation_key) {
        last_nav_key_ = -1;
        const char* label = KeyLabel(page_, selected_index_);
        if (label && label[0]) {
            if (!profile_.space_label.empty() && std::strcmp(label, profile_.space_label.c_str()) == 0) {
                last_output_ = profile_.space_output;
            } else {
                last_output_ = label;
            }
            if (on_key_) {
                on_key_(this, last_output_.c_str(), on_key_ctx_);
            }
        }
        return InputResult::Consume;
    }

    if (profile_.page_switch_enabled && key == profile_.page_switch_key) {
        last_nav_key_ = -1;
        page_ = (page_ + 1) % ResolveKeyboardPages(profile_);
        MarkDirty();
        return InputResult::Consume;
    }

    return InputResult::Continue;
}

void SoftKeyboardWidget::SetOnKey(KeyCallback callback, void* ctx) {
    on_key_ = callback;
    on_key_ctx_ = ctx;
}

int SoftKeyboardWidget::SelectedIndex() const {
    return selected_index_;
}

void SoftKeyboardWidget::SetSelectedIndex(int index) {
    selected_index_ = index;
    ClampSelection();
    profile_.selected_index = selected_index_;
    MarkDirty();
}

const std::string& SoftKeyboardWidget::LastOutput() const {
    return last_output_;
}

void SoftKeyboardWidget::ClampSelection() {
    selected_index_ = ClampIndexByCount(selected_index_, ResolveKeyboardPageSize(profile_));
}

const char* SoftKeyboardWidget::KeyLabel(int page, int index) const {
    const int page_size = ResolveKeyboardPageSize(profile_);
    const int pages = ResolveKeyboardPages(profile_);
    if (index < 0 || index >= page_size || page < 0 || page >= pages) {
        return "";
    }

    if (HasCustomKeyboardLayout(profile_)) {
        const size_t flat_index = static_cast<size_t>(page * page_size + index);
        if (flat_index >= profile_.key_labels.size()) {
            return "";
        }
        return profile_.key_labels[flat_index].c_str();
    }

    if (ResolveKeyboardRows(profile_) == 4 &&
        ResolveKeyboardCols(profile_) == 9 &&
        ResolveKeyboardPages(profile_) <= 3) {
        return DefaultKeyboardLabel(page, index);
    }
    return "";
}

void SoftKeyboardWidget::OnDraw(Painter& p) {
    ClampSelection();
    Rect rect = LocalRect();
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    p.SetDrawColor(Color::White);
    p.FillRect(rect);

    const int rows = ResolveKeyboardRows(profile_);
    const int cols = ResolveKeyboardCols(profile_);
    const GridLayoutMetrics metrics = MakeGridLayoutMetrics(rect, rows, cols);

    for (int r = 0; r < metrics.rows; ++r) {
        const int y = r * metrics.cell_h;
        const int h = (r == metrics.rows - 1) ? (rect.h - y) : metrics.cell_h;
        for (int c = 0; c < metrics.cols; ++c) {
            const int x = c * metrics.cell_w;
            const int w = (c == metrics.cols - 1) ? (rect.w - x) : metrics.cell_w;
            const int index = r * cols + c;
            const bool selected = (index == selected_index_);
            p.SetDrawColor(selected ? Color::Black : Color::White);
            p.FillRect({static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)});
            p.SetTextColor(selected ? Color::White : Color::Black);

            const char* label = KeyLabel(page_, index);
            if (label && label[0]) {
                DrawCenteredTextInCell(p, x, y, w, h, label);
            }
        }
    }
    if (profile_.draw_grid_outline) {
        DrawGridOutline(p, rect, metrics);
    }
}

void TopBarWidget::OnDraw(Painter& p) {
    auto* epd_painter = dynamic_cast<EpdPainter*>(&p);
    if (profile_.render_mode != BarProfile::RenderMode::Native || !epd_painter || !epd_painter->Epd()) {
        DrawGenericBar(p, LocalRect(), text_, profile_);
        return;
    }

    eteacher::app_menu::MenuStyle style;
    const Rect rect = LocalRect();
    if (rect.h > 0) {
        style.top_height = rect.h;
    }
    const auto status = eteacher::app_ui::BuildMenuStatus(Board::GetInstance());
    eteacher::app_ui::DrawTopBar(epd_painter->Gfx(), epd_painter->Epd(), style, status);
}

void TopBarWidget::SetProfile(const BarProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const BarProfile& TopBarWidget::Profile() const {
    return profile_;
}

void BottomBarWidget::OnDraw(Painter& p) {
    auto* epd_painter = dynamic_cast<EpdPainter*>(&p);
    if (profile_.render_mode != BarProfile::RenderMode::Native || !epd_painter || !epd_painter->Epd()) {
        DrawGenericBar(p, LocalRect(), text_, profile_);
        return;
    }

    eteacher::app_menu::MenuStyle style;
    const Rect rect = LocalRect();
    if (rect.h > 0) {
        style.bottom_height = rect.h;
    }
    eteacher::app_ui::DrawBottomBar(epd_painter->Gfx(), epd_painter->Epd(), style, text_);
}

void BottomBarWidget::SetProfile(const BarProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const BarProfile& BottomBarWidget::Profile() const {
    return profile_;
}

void CheckboxWidget::SetProfile(const CheckboxProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const CheckboxProfile& CheckboxWidget::Profile() const {
    return profile_;
}

void CheckboxWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    const bool focused = Focused() && profile_.focus_invert;
    p.SetDrawColor(focused ? Color::Black : Color::White);
    p.FillRect(rect);
    p.SetDrawColor(focused ? Color::White : Color::Black);
    p.SetTextColor(focused ? Color::White : Color::Black);
    const int target_box = std::max<int>(1, profile_.box_size);
    const int box = std::min(target_box, rect.h > 0 ? rect.h : target_box);
    const int text_h = std::max<int>(1, p.MeasureText("A", nullptr).h);
    const int box_y = std::max(0, (text_h - box) / 2);
    p.DrawRect({0, static_cast<int16_t>(box_y), static_cast<int16_t>(box), static_cast<int16_t>(box)});
    if (profile_.checked) {
        const int x1 = std::max(1, box / 4);
        const int y1 = box_y + std::max(1, box / 2);
        const int x2 = std::max(x1 + 1, box / 2 - 1);
        const int y2 = box_y + std::max(3, box - 3);
        const int x3 = std::max(x2 + 1, box - 3);
        for (int i = 0; i <= (x2 - x1); ++i) {
            p.FillRect({static_cast<int16_t>(x1 + i), static_cast<int16_t>(y1 + i), 1, 1});
        }
        for (int i = 0; i <= (x3 - x2); ++i) {
            p.FillRect({static_cast<int16_t>(x2 + i), static_cast<int16_t>(y2 - i), 1, 1});
        }
    }
    const int16_t text_x = static_cast<int16_t>(box + std::max<int16_t>(0, profile_.text_offset_x));
    p.DrawText({text_x, 0}, text_.c_str());
}

void RadioWidget::SetProfile(const RadioProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const RadioProfile& RadioWidget::Profile() const {
    return profile_;
}

void RadioWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    const int target_radius = std::max<int>(1, profile_.radius);
    const int radius = std::min(target_radius, std::max<int>(1, rect.h / 2));
    const bool focused = Focused() && profile_.focus_invert;
    p.SetDrawColor(focused ? Color::Black : Color::White);
    p.FillRect(rect);
    p.SetDrawColor(focused ? Color::White : Color::Black);
    p.SetTextColor(focused ? Color::White : Color::Black);
    const int cx = radius + 1;
    const int cy = radius + 2;
    p.DrawCircle({static_cast<int16_t>(cx), static_cast<int16_t>(cy)}, radius);
    if (profile_.checked) {
        const int outer_diameter = radius * 2;
        const int dot_diameter = std::max(1, outer_diameter / 2);
        const int dot_radius = std::max(1, dot_diameter / 2);
        for (int dy = -dot_radius; dy <= dot_radius; ++dy) {
            for (int dx = -dot_radius; dx <= dot_radius; ++dx) {
                if (dx * dx + dy * dy <= dot_radius * dot_radius) {
                    p.FillRect({static_cast<int16_t>(cx + dx), static_cast<int16_t>(cy + dy), 1, 1});
                }
            }
        }
    }
    const int16_t text_x = static_cast<int16_t>(radius * 2 + std::max<int16_t>(0, profile_.text_offset_x));
    p.DrawText({text_x, 0}, text_.c_str());
}

void SwitchWidget::SetProfile(const SwitchProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const SwitchProfile& SwitchWidget::Profile() const {
    return profile_;
}

void SwitchWidget::OnDraw(Painter& p) {
    const Rect rect = LocalRect();
    const bool focused = Focused() && profile_.focus_invert;
    p.SetDrawColor(focused ? Color::Black : Color::White);
    p.FillRect(rect);

    const int box_w = std::max<int>(1, profile_.box_width);
    const int target_box_h = std::max<int>(1, profile_.box_height);
    const int box_h = std::min(target_box_h, rect.h > 0 ? rect.h : target_box_h);
    const bool is_on = profile_.checked;
    const char* on_label = profile_.on_label.empty() ? "ON" : profile_.on_label.c_str();
    const char* off_label = profile_.off_label.empty() ? "OFF" : profile_.off_label.c_str();
    const int16_t state_text_x = std::max<int16_t>(0, profile_.state_text_offset_x);
    const int16_t state_text_y = std::max<int16_t>(0, profile_.state_text_offset_y);
    const int16_t state_inset = std::max<int16_t>(0, profile_.state_inset);

    p.SetDrawColor(Color::Black);
    p.DrawRect({0, 0, static_cast<int16_t>(box_w), static_cast<int16_t>(box_h)});
    if (is_on && profile_.invert_when_on) {
        const int16_t inner_w = static_cast<int16_t>(std::max<int>(0, box_w - state_inset * 2));
        const int16_t inner_h = static_cast<int16_t>(std::max<int>(0, box_h - state_inset * 2));
        p.InvertRect({state_inset, state_inset, inner_w, inner_h});
        p.SetTextColor(Color::White);
        p.DrawText({state_text_x, state_text_y}, on_label);
        p.SetTextColor(Color::Black);
    } else if (is_on) {
        p.SetTextColor(Color::Black);
        p.DrawText({state_text_x, state_text_y}, on_label);
    } else {
        p.SetTextColor(Color::Black);
        p.DrawText({state_text_x, state_text_y}, off_label);
    }
    p.SetTextColor(focused ? Color::White : Color::Black);
    p.DrawText({static_cast<int16_t>(box_w + std::max<int16_t>(0, profile_.text_offset_x)), 0}, text_.c_str());
}

void ProgressWidget::SetProfile(const ProgressProfile& profile) {
    profile_ = profile;
    if (profile_.max_value == 0) {
        profile_.max_value = 100;
    }
    profile_.value = ClampProgressValue(profile_.value, profile_.max_value);
    MarkMeasureDirty();
    MarkDirty();
}

const ProgressProfile& ProgressWidget::Profile() const {
    return profile_;
}

void ProgressWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    const uint8_t max_value = profile_.max_value == 0 ? 100 : profile_.max_value;
    const int percent = static_cast<int>(ClampProgressValue(profile_.value, max_value)) * 100 / max_value;
    const int inset = std::max<int>(0, profile_.fill_inset);
    if (profile_.draw_border) {
        p.SetDrawColor(Color::Black);
        p.DrawRect(rect);
    }
    const int inner_x = inset;
    const int inner_y = inset;
    const int inner_w = std::max(0, rect.w - inset * 2);
    const int inner_h = std::max(0, rect.h - inset * 2);
    if (inner_w > 0 && inner_h > 0 && percent > 0) {
        const int fill_w = inner_w * percent / 100;
        p.SetDrawColor(Color::Black);
        p.FillRect({static_cast<int16_t>(inner_x),
                    static_cast<int16_t>(inner_y),
                    static_cast<int16_t>(fill_w),
                    static_cast<int16_t>(inner_h)});
    }
}

} // namespace app_ui
