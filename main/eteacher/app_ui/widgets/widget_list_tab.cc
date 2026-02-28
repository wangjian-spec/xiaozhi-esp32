#include "widget_internal.h"

#include "../input.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_ui/common_ui_utils.h"

#include <algorithm>

namespace app_ui {
using namespace widget_internal;

TabViewWidget::TabViewWidget() {
    SetFontName("wenquanyi_11pt");
}

void TabViewWidget::SetProfile(const TabViewProfile& profile) {
    profile_ = profile;
    if (profile_.rows <= 0) {
        profile_.rows = 1;
    }
    ClampSelection();
    MarkMeasureDirty();
    MarkDirty();
}

const TabViewProfile& TabViewWidget::Profile() const {
    return profile_;
}

InputResult ListViewWidget::OnInput(const InputEvent& e, InputPhase phase) {
    ListViewBehavior* behavior = behavior_ ? behavior_.get() : &DefaultListViewBehaviorInstance();
    return behavior->OnInput(*this, e, phase);
}

FocusIntent ListViewWidget::OnFocusKey(KeyCode key) {
    ListViewBehavior* behavior = behavior_ ? behavior_.get() : &DefaultListViewBehaviorInstance();
    return behavior->OnFocusKey(*this, key);
}

void ListViewWidget::SetItems(std::vector<std::string> items) {
    model_ = MakeStaticVectorModel(std::move(items));
    ClampSelection();
    MarkMeasureDirty();
    MarkDirty();
}

void ListViewWidget::SetItemModel(std::unique_ptr<ItemModel> model) {
    model_ = std::move(model);
    ClampSelection();
    MarkMeasureDirty();
    MarkDirty();
}

void ListViewWidget::SetProfile(const ListViewProfile& profile) {
    profile_ = profile;
    if (profile_.cols <= 0) {
        profile_.cols = 1;
    }
    ClampSelection();
    MarkMeasureDirty();
    MarkDirty();
}

const ListViewProfile& ListViewWidget::Profile() const {
    return profile_;
}

void ListViewWidget::SetBehavior(std::unique_ptr<ListViewBehavior> behavior) {
    behavior_ = std::move(behavior);
    MarkMeasureDirty();
    MarkDirty();
}

void ListViewWidget::ResetBehavior() {
    behavior_.reset();
    MarkMeasureDirty();
    MarkDirty();
}

int ListViewWidget::SelectedIndex() const {
    return selected_index_;
}

void ListViewWidget::SetSelectedIndex(int index) {
    selected_index_ = index;
    ClampSelection();
    MarkDirty();
}

std::string ListViewWidget::SelectedItem() const {
    const char* label = ItemLabel(selected_index_);
    if (!label) {
        return {};
    }
    return std::string(label);
}

void ListViewWidget::ActivateSelected() {
    ClampSelection();
    if (on_activated_) {
        on_activated_(this, selected_index_, on_activated_ctx_);
    }
}

void ListViewWidget::SetOnActivated(ActivateCallback callback, void* ctx) {
    on_activated_ = callback;
    on_activated_ctx_ = ctx;
}

void ListViewWidget::OnTextChanged() {
    SetModelFromText();
}

void ListViewWidget::SetModelFromText() {
    model_ = MakeTextParsedModel(text_);
    ClampSelection();
}

int ListViewWidget::ItemCount() const {
    return model_ ? model_->Count() : 0;
}

const char* ListViewWidget::ItemLabel(int index) const {
    if (!model_) {
        return "";
    }
    const char* label = model_->Label(index);
    return label ? label : "";
}

void ListViewWidget::ClampSelection() {
    selected_index_ = ClampIndexByCount(selected_index_, ItemCount());
}

void ListViewWidget::OnDraw(Painter& p) {
    ListViewBehavior* behavior = behavior_ ? behavior_.get() : &DefaultListViewBehaviorInstance();
    behavior->OnDraw(*this, p);
}

void TabViewWidget::OnDraw(Painter& p) {
    ClampSelection();
    Rect rect = LocalRect();
    const int rows = EffectiveRows();
    const int cols = EffectiveCols();
    if (rows <= 0 || cols <= 0 || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const GridLayoutMetrics metrics = MakeGridLayoutMetrics(rect, rows, cols);
    const bool focused = Focused();
    auto* epd_painter = dynamic_cast<EpdPainter*>(&p);

    for (int r = 0; r < metrics.rows; ++r) {
        const int y = r * metrics.cell_h;
        if (y >= rect.h) {
            break;
        }
        for (int c = 0; c < metrics.cols; ++c) {
            const int index = r * metrics.cols + c;
            const int x = c * metrics.cell_w;
            const int w = (c == metrics.cols - 1) ? (rect.w - x) : metrics.cell_w;
            const int h = (r == metrics.rows - 1) ? std::min(metrics.cell_h, rect.h - y) : metrics.cell_h;
            const bool selected = focused && profile_.focus_highlight_enabled && (index == selected_index_);

            p.SetDrawColor(selected ? Color::Black : Color::White);
            p.FillRect({static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)});
            p.SetTextColor(selected ? Color::White : Color::Black);

            if (index < ItemCount()) {
                const char* label = ItemLabel(index);
                if (epd_painter && epd_painter->Epd()) {
                    const char* font_name = epd_painter->FontName();
                    if (!font_name || !font_name[0]) {
                        font_name = kDefaultFontName;
                    }
                    const int16_t text_w = static_cast<int16_t>(epd_painter->Epd()->MeasureUtf8Width(label, font_name));
                    const int16_t font_h = eteacher::app_ui::GetFontHeight(font_name);
                    const int16_t font_ascent = eteacher::app_ui::GetFontAscent(font_name);
                    const int16_t font_descent = static_cast<int16_t>(font_h - font_ascent);
                    int16_t tx = static_cast<int16_t>(x + (w - text_w) / 2);
                    const int16_t glyph_center_offset = static_cast<int16_t>((font_ascent - font_descent) / 2);
                    int16_t baseline = static_cast<int16_t>(y + h / 2 + glyph_center_offset);
                    if (text_w > w) {
                        tx = static_cast<int16_t>(x + 2);
                    }
                    if (font_h > h) {
                        baseline = static_cast<int16_t>(y + 2 + font_ascent);
                    }
                    const Point offset = epd_painter->Offset();
                    const uint16_t text_color = selected ? GxEPD_WHITE : GxEPD_BLACK;
                    epd_painter->Epd()->DrawUtf8(static_cast<int16_t>(tx + offset.x),
                                                 static_cast<int16_t>(baseline + offset.y),
                                                 label,
                                                 font_name,
                                                 text_color);
                } else {
                    DrawCenteredTextInCell(p, x, y, w, h, label);
                }
            }
        }
    }
    DrawGridOutline(p, rect, metrics);
}

InputResult TabViewWidget::OnInput(const InputEvent& e, InputPhase phase) {
    (void)e;
    (void)phase;
    return InputResult::Continue;
}

FocusIntent TabViewWidget::OnFocusKey(KeyCode key) {
    ClampSelection();
    if (ItemCount() <= 0) {
        return FocusIntent::Bubble;
    }
    if (!profile_.selection_enabled) {
        return FocusIntent::Bubble;
    }

    const int count = ItemCount();
    const int old_index = selected_index_;
    const int cols = std::max(1, EffectiveCols());

    auto move_linear = [&](int delta, FocusIntent escape) -> FocusIntent {
        int next = old_index + delta;
        if (profile_.wrap_navigation && count > 0) {
            next %= count;
            if (next < 0) {
                next += count;
            }
            SetSelectedIndex(next);
            return FocusIntent::Consume;
        }
        if (next >= 0 && next < count) {
            SetSelectedIndex(next);
            return FocusIntent::Consume;
        }
        return escape;
    };

    switch (profile_.navigation_mode) {
        case TabViewProfile::NavigationMode::Vertical:
            if (key == KeyCode::Up) {
                return move_linear(-1, FocusIntent::EscapeUp);
            }
            if (key == KeyCode::Down) {
                return move_linear(1, FocusIntent::EscapeDown);
            }
            if (key == KeyCode::Left) {
                return FocusIntent::EscapeLeft;
            }
            if (key == KeyCode::Right) {
                return FocusIntent::EscapeRight;
            }
            break;

        case TabViewProfile::NavigationMode::Horizontal:
            if (key == KeyCode::Left) {
                return move_linear(-1, FocusIntent::EscapeLeft);
            }
            if (key == KeyCode::Right) {
                return move_linear(1, FocusIntent::EscapeRight);
            }
            if (key == KeyCode::Up) {
                return FocusIntent::EscapeUp;
            }
            if (key == KeyCode::Down) {
                return FocusIntent::EscapeDown;
            }
            break;

        case TabViewProfile::NavigationMode::Grid: {
            const int row = old_index / cols;
            const int col = old_index % cols;
            if (key == KeyCode::Left) {
                if (col > 0) {
                    SetSelectedIndex(old_index - 1);
                    return FocusIntent::Consume;
                }
                if (profile_.wrap_navigation) {
                    const int last_col = std::min(cols - 1, count - row * cols - 1);
                    SetSelectedIndex(row * cols + std::max(0, last_col));
                    return FocusIntent::Consume;
                }
                return FocusIntent::EscapeLeft;
            }
            if (key == KeyCode::Right) {
                if (old_index + 1 < count && col + 1 < cols) {
                    SetSelectedIndex(old_index + 1);
                    return FocusIntent::Consume;
                }
                if (profile_.wrap_navigation) {
                    SetSelectedIndex(row * cols);
                    return FocusIntent::Consume;
                }
                return FocusIntent::EscapeRight;
            }
            if (key == KeyCode::Up) {
                if (row > 0) {
                    SetSelectedIndex(old_index - cols);
                    return FocusIntent::Consume;
                }
                if (profile_.wrap_navigation) {
                    int next = old_index;
                    while (next + cols < count) {
                        next += cols;
                    }
                    SetSelectedIndex(next);
                    return FocusIntent::Consume;
                }
                return FocusIntent::EscapeUp;
            }
            if (key == KeyCode::Down) {
                if (old_index + cols < count) {
                    SetSelectedIndex(old_index + cols);
                    return FocusIntent::Consume;
                }
                if (profile_.wrap_navigation) {
                    SetSelectedIndex(col < count ? col : 0);
                    return FocusIntent::Consume;
                }
                return FocusIntent::EscapeDown;
            }
            break;
        }
    }

    return FocusIntent::None;
}

void TabViewWidget::SetItems(std::vector<std::string> items) {
    model_ = MakeStaticVectorModel(std::move(items));
    ClampSelection();
    MarkMeasureDirty();
    MarkDirty();
}

void TabViewWidget::SetItemModel(std::unique_ptr<ItemModel> model) {
    model_ = std::move(model);
    ClampSelection();
    MarkMeasureDirty();
    MarkDirty();
}

int TabViewWidget::SelectedIndex() const {
    return selected_index_;
}

void TabViewWidget::SetSelectedIndex(int index) {
    const int count = ItemCount();
    const int clamped = (count <= 0) ? std::max(0, index) : ClampIndexByCount(index, count);
    if (selected_index_ == clamped) {
        return;
    }
    selected_index_ = clamped;
    MarkDirty();
}

std::string TabViewWidget::SelectedItem() const {
    const char* label = ItemLabel(selected_index_);
    if (!label) {
        return {};
    }
    return std::string(label);
}

void TabViewWidget::OnTextChanged() {
    SetModelFromText();
}

void TabViewWidget::SetModelFromText() {
    model_ = MakeTextParsedModel(text_);
    ClampSelection();
}

int TabViewWidget::ItemCount() const {
    return model_ ? model_->Count() : 0;
}

const char* TabViewWidget::ItemLabel(int index) const {
    if (!model_) {
        return "";
    }
    const char* label = model_->Label(index);
    return label ? label : "";
}

void TabViewWidget::ClampSelection() {
    selected_index_ = ClampIndexByCount(selected_index_, ItemCount());
}

int TabViewWidget::EffectiveRows() const {
    if (profile_.rows > 0) {
        return profile_.rows;
    }
    return 1;
}

int TabViewWidget::EffectiveCols() const {
    if (profile_.cols > 0) {
        return profile_.cols;
    }
    const int rows = EffectiveRows();
    if (rows <= 0) {
        return 1;
    }
    if (ItemCount() <= 0) {
        return 1;
    }
    return static_cast<int>((ItemCount() + rows - 1) / rows);
}

} // namespace app_ui
