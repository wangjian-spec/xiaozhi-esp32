#include "eteacher/app_service/tool/ui_widget.h"

#include <Adafruit_GFX.h>
#include <algorithm>
#include <limits>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/font_manager/font_manager.h"

namespace eteacher::app_service::tool {
namespace {

int16_t GetFontHeight(std::string_view font_name) {
    const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
    if (!font || !font->Ready()) {
        return 12;
    }
    const auto& header = font->Header();
    return static_cast<int16_t>(header.ascent + header.descent);
}

int16_t GetFontAscent(std::string_view font_name) {
    const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
    if (!font || !font->Ready()) {
        return 9;
    }
    return static_cast<int16_t>(font->Header().ascent);
}

size_t Utf8Next(std::string_view text, size_t index) {
    if (index >= text.size()) {
        return text.size();
    }
    const unsigned char c = static_cast<unsigned char>(text[index]);
    if (c < 0x80) return index + 1;
    if ((c & 0xE0) == 0xC0) return std::min(text.size(), index + 2);
    if ((c & 0xF0) == 0xE0) return std::min(text.size(), index + 3);
    if ((c & 0xF8) == 0xF0) return std::min(text.size(), index + 4);
    return std::min(text.size(), index + 1);
}

void SplitByNewline(std::string_view text, std::vector<std::string>& out) {
    out.clear();
    size_t start = 0;
    while (start <= text.size()) {
        const size_t pos = text.find('\n', start);
        if (pos == std::string_view::npos) {
            out.emplace_back(text.substr(start));
            break;
        }
        out.emplace_back(text.substr(start, pos - start));
        start = pos + 1;
    }
}

std::string TruncateToWidth(std::string_view text,
                            int16_t max_w,
                            CustomEpdDisplay* epd,
                            std::string_view font,
                            bool ellipsis) {
    if (!epd || max_w <= 0) return std::string();
    const int16_t width = epd->MeasureUtf8Width(text, font);
    if (width <= max_w) return std::string(text);

    const std::string ell = ellipsis ? "..." : "";
    const int16_t ell_w = ellipsis ? epd->MeasureUtf8Width(ell, font) : 0;
    if (ellipsis && ell_w > max_w) return std::string();

    size_t i = 0;
    size_t last_ok = 0;
    while (i < text.size()) {
        const size_t next = Utf8Next(text, i);
        const std::string_view part = text.substr(0, next);
        if (epd->MeasureUtf8Width(part, font) + ell_w > max_w) {
            break;
        }
        last_ok = next;
        i = next;
    }
    std::string out(text.substr(0, last_ok));
    if (ellipsis) out += ell;
    return out;
}

void WrapText(std::string_view text,
              int16_t max_w,
              CustomEpdDisplay* epd,
              std::string_view font,
              std::vector<std::string>& out) {
    out.clear();
    if (!epd || max_w <= 0) {
        out.emplace_back("");
        return;
    }

    std::string line;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\n') {
            out.emplace_back(line);
            line.clear();
            ++i;
            continue;
        }
        const size_t next = Utf8Next(text, i);
        const std::string_view ch = text.substr(i, next - i);

        std::string candidate = line;
        candidate.append(ch.data(), ch.size());
        if (epd->MeasureUtf8Width(candidate, font) <= max_w || line.empty()) {
            line = candidate;
            i = next;
            continue;
        }
        out.emplace_back(line);
        line.clear();
    }
    if (!line.empty() || text.empty()) {
        out.emplace_back(line);
    }
}

eteacher::layout::Rect IntersectRect(const eteacher::layout::Rect& a, const eteacher::layout::Rect& b) {
    const int16_t x1 = std::max<int16_t>(a.x, b.x);
    const int16_t y1 = std::max<int16_t>(a.y, b.y);
    const int16_t x2 = std::min<int16_t>(a.right(), b.right());
    const int16_t y2 = std::min<int16_t>(a.bottom(), b.bottom());
    if (x2 <= x1 || y2 <= y1) return {};
    return {x1, y1, static_cast<int16_t>(x2 - x1), static_cast<int16_t>(y2 - y1)};
}

eteacher::layout::Rect UnionRect(const eteacher::layout::Rect& a, const eteacher::layout::Rect& b) {
    if (a.IsEmpty()) return b;
    if (b.IsEmpty()) return a;
    const int16_t x1 = std::min<int16_t>(a.x, b.x);
    const int16_t y1 = std::min<int16_t>(a.y, b.y);
    const int16_t x2 = std::max<int16_t>(a.right(), b.right());
    const int16_t y2 = std::max<int16_t>(a.bottom(), b.bottom());
    return {x1, y1, static_cast<int16_t>(x2 - x1), static_cast<int16_t>(y2 - y1)};
}

} // namespace

void WidgetBase::AttachRegion(const eteacher::layout::Region* region) {
    region_ = region;
    MarkDirty(rect());
}

eteacher::layout::Rect WidgetBase::rect() const {
    if (!region_) return {};
    return region_->rect;
}

void WidgetBase::ClearDirty() {
    dirty_ = false;
    dirty_rect_ = {};
}

void WidgetBase::MarkDirty(const eteacher::layout::Rect& rect) {
    dirty_ = true;
    const eteacher::layout::Rect target = rect.IsEmpty() ? this->rect() : rect;
    dirty_rect_ = UnionRect(dirty_rect_, target);
}

void LabelWidget::SetText(std::string text) {
    text_ = std::move(text);
    MarkDirty(rect());
}

void LabelWidget::Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) {
    const auto r = rect();
    if (r.IsEmpty() || !epd) {
        return;
    }

    gfx.fillRect(r.x, r.y, r.w, r.h, GxEPD_WHITE);

    const int16_t line_h = std::max<int16_t>(1, GetFontHeight(font_));
    const int16_t ascent = GetFontAscent(font_);
    const int max_lines = std::max<int>(1, r.h / line_h);
    const int16_t max_w = static_cast<int16_t>(r.w - padding_ * 2);

    std::vector<std::string> lines;
    if (overflow_ == TextOverflow::Wrap) {
        WrapText(text_, max_w, epd, font_, lines);
    } else {
        SplitByNewline(text_, lines);
        for (auto& line : lines) {
            line = TruncateToWidth(line, max_w, epd, font_, false);
        }
    }

    if (static_cast<int>(lines.size()) > max_lines) {
        lines.resize(max_lines);
        if (!lines.empty()) {
            lines.back() = TruncateToWidth(lines.back(), max_w, epd, font_, true);
        }
    }

    int16_t y = static_cast<int16_t>(r.y + ascent);
    for (int i = 0; i < static_cast<int>(lines.size()) && i < max_lines; ++i) {
        const auto& line = lines[i];
        int16_t x = static_cast<int16_t>(r.x + padding_);
        if (align_ == TextAlign::Center) {
            const int16_t w = epd->MeasureUtf8Width(line, font_);
            x = static_cast<int16_t>(r.x + (r.w - w) / 2);
        }
        epd->DrawUtf8(x, y, line, font_, GxEPD_BLACK);
        y = static_cast<int16_t>(y + line_h);
    }

    ClearDirty();
}

void ButtonWidget::SetLabel(std::string label) {
    label_ = std::move(label);
    MarkDirty(rect());
}

bool ButtonWidget::OnAction(Action action) {
    if (action == Action::Confirm) {
        if (on_confirm_) {
            on_confirm_();
        }
        return true;
    }
    return false;
}

void ButtonWidget::OnFocus(bool focused) {
    WidgetBase::OnFocus(focused);
    MarkDirty(rect());
}

void ButtonWidget::Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) {
    const auto r = rect();
    if (r.IsEmpty() || !epd) return;

    const bool focused = this->focused();
    gfx.fillRect(r.x, r.y, r.w, r.h, focused ? GxEPD_BLACK : GxEPD_WHITE);
    gfx.drawRect(r.x, r.y, r.w, r.h, GxEPD_BLACK);

    const int16_t ascent = GetFontAscent(font_);
    const int16_t text_w = epd->MeasureUtf8Width(label_, font_);
    const int16_t x = static_cast<int16_t>(r.x + (r.w - text_w) / 2);
    const int16_t y = static_cast<int16_t>(r.y + (r.h - GetFontHeight(font_)) / 2 + ascent);
    epd->DrawUtf8(x, y, label_, font_, focused ? GxEPD_WHITE : GxEPD_BLACK);

    ClearDirty();
}

void ListWidget::SetItems(const std::vector<std::string>* items) {
    items_ = items;
    selected_index_ = 0;
    top_index_ = 0;
    last_selected_ = 0;
    last_top_ = 0;
    MarkDirty(rect());
}

int ListWidget::item_count() const {
    return items_ ? static_cast<int>(items_->size()) : 0;
}

void ListWidget::SetSelected(int index) {
    selected_index_ = index;
    ClampSelection();
    MarkDirty(rect());
}

bool ListWidget::OnAction(Action action) {
    if (item_count() <= 0) return false;
    if (action != Action::Up && action != Action::Down) return false;

    const int prev = selected_index_;
    const int prev_top = top_index_;
    const int16_t line_h = std::max<int16_t>(1, GetFontHeight(font_));

    if (action == Action::Up) {
        selected_index_ = std::max(0, selected_index_ - 1);
    } else {
        selected_index_ = std::min(item_count() - 1, selected_index_ + 1);
    }
    EnsureSelectionVisible(line_h);
    UpdateDirtyOnSelectionChange(prev, prev_top, line_h);

    return true;
}

void ListWidget::Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) {
    const auto r = rect();
    if (r.IsEmpty() || !epd) return;

    const int16_t line_h = std::max<int16_t>(1, GetFontHeight(font_));
    const int visible = VisibleCount(line_h);
    const int count = item_count();
    const int end = std::min(count, top_index_ + visible);

    const auto draw_area = HasDirty() ? DirtyRect() : r;
    if (!draw_area.IsEmpty()) {
        gfx.fillRect(draw_area.x, draw_area.y, draw_area.w, draw_area.h, GxEPD_WHITE);
    }

    for (int idx = top_index_; idx < end; ++idx) {
        const auto item_rect = ItemRect(idx, line_h);
        if (draw_area.IsEmpty() || IntersectRect(item_rect, draw_area).IsEmpty()) {
            if (!draw_area.IsEmpty()) continue;
        }

        const bool selected = (idx == selected_index_);
        if (selected && invert_highlight_) {
            gfx.fillRect(item_rect.x, item_rect.y, item_rect.w, item_rect.h, GxEPD_BLACK);
        } else if (selected) {
            gfx.drawRect(item_rect.x + 1, item_rect.y + 1, item_rect.w - 2, item_rect.h - 2, GxEPD_BLACK);
        }

        const std::string& text = (*items_)[idx];
        const int16_t ascent = GetFontAscent(font_);
        const int16_t text_x = static_cast<int16_t>(item_rect.x + padding_);
        const int16_t text_y = static_cast<int16_t>(item_rect.y + ascent + (line_h - GetFontHeight(font_)) / 2);
        epd->DrawUtf8(text_x, text_y, text, font_, selected && invert_highlight_ ? GxEPD_WHITE : GxEPD_BLACK);
    }

    last_selected_ = selected_index_;
    last_top_ = top_index_;
    ClearDirty();
}

int ListWidget::VisibleCount(int16_t line_h) const {
    const auto r = rect();
    if (r.IsEmpty() || line_h <= 0) return 0;
    return std::max(1, r.h / line_h);
}

void ListWidget::ClampSelection() {
    const int count = item_count();
    if (count <= 0) {
        selected_index_ = 0;
        top_index_ = 0;
        return;
    }
    selected_index_ = std::max(0, std::min(selected_index_, count - 1));
    if (top_index_ > selected_index_) {
        top_index_ = selected_index_;
    }
}

void ListWidget::EnsureSelectionVisible(int16_t line_h) {
    const int visible = VisibleCount(line_h);
    if (visible <= 0) return;
    if (selected_index_ < top_index_) {
        top_index_ = selected_index_;
    } else if (selected_index_ >= top_index_ + visible) {
        top_index_ = selected_index_ - visible + 1;
    }
}

eteacher::layout::Rect ListWidget::ItemRect(int index, int16_t line_h) const {
    const auto r = rect();
    if (r.IsEmpty() || line_h <= 0) return {};
    const int local = index - top_index_;
    if (local < 0) return {};
    const int16_t y = static_cast<int16_t>(r.y + local * line_h);
    if (y >= r.bottom()) return {};
    return {r.x, y, r.w, line_h};
}

void ListWidget::UpdateDirtyOnSelectionChange(int previous, int previous_top, int16_t line_h) {
    if (previous_top != top_index_) {
        MarkDirty(rect());
        return;
    }
    const auto a = ItemRect(previous, line_h);
    const auto b = ItemRect(selected_index_, line_h);
    MarkDirty(UnionRect(a, b));
}

void TextInputWidget::SetText(std::string text) {
    text_ = std::move(text);
    MarkDirty(rect());
}

void TextInputWidget::AppendText(std::string_view utf8) {
    text_.append(utf8.data(), utf8.size());
    MarkDirty(rect());
}

void TextInputWidget::Backspace() {
    if (text_.empty()) return;
    size_t i = 0;
    size_t last = 0;
    while (i < text_.size()) {
        last = i;
        i = Utf8Next(text_, i);
    }
    text_.erase(last);
    MarkDirty(rect());
}

bool TextInputWidget::OnAction(Action action) {
    if (action == Action::Delete) {
        Backspace();
        return true;
    }
    return false;
}

void TextInputWidget::Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) {
    const auto r = rect();
    if (r.IsEmpty() || !epd) return;

    gfx.fillRect(r.x, r.y, r.w, r.h, GxEPD_WHITE);
    gfx.drawRect(r.x, r.y, r.w, r.h, GxEPD_BLACK);

    const int16_t ascent = GetFontAscent(font_);
    const int16_t max_w = static_cast<int16_t>(r.w - padding_ * 2);
    std::string display = text_;
    if (epd->MeasureUtf8Width(display, font_) > max_w) {
        // Trim from left to keep tail visible.
        size_t cut = 0;
        size_t i = 0;
        while (i < display.size()) {
            const size_t next = Utf8Next(display, i);
            const std::string_view tail = std::string_view(display).substr(next);
            if (epd->MeasureUtf8Width(tail, font_) <= max_w) {
                cut = next;
                break;
            }
            i = next;
        }
        display = display.substr(cut);
    }

    const int16_t text_x = static_cast<int16_t>(r.x + padding_);
    const int16_t text_y = static_cast<int16_t>(r.y + (r.h - GetFontHeight(font_)) / 2 + ascent);
    epd->DrawUtf8(text_x, text_y, display, font_, GxEPD_BLACK);

    if (focused() && cursor_visible_) {
        const int16_t text_w = epd->MeasureUtf8Width(display, font_);
        const int16_t cx = static_cast<int16_t>(text_x + text_w + 1);
        const int16_t cy = static_cast<int16_t>(text_y - ascent);
        gfx.drawFastVLine(cx, cy, GetFontHeight(font_), GxEPD_BLACK);
    }

    ClearDirty();
}

void DialogWidget::SetTitle(std::string title) {
    title_ = std::move(title);
    MarkDirty(rect());
}

void DialogWidget::SetContent(std::string content) {
    content_ = std::move(content);
    MarkDirty(rect());
}

void DialogWidget::SetButtons(const std::vector<std::string>* buttons) {
    buttons_ = buttons;
    selected_index_ = 0;
    ClampSelection();
    MarkDirty(rect());
}

bool DialogWidget::OnAction(Action action) {
    if (action == Action::Left) {
        if (buttons_ && !buttons_->empty()) {
            selected_index_ = std::max(0, selected_index_ - 1);
            ClampSelection();
            MarkDirty(rect());
        }
        return true;
    }
    if (action == Action::Right) {
        if (buttons_ && !buttons_->empty()) {
            selected_index_ = std::min(static_cast<int>(buttons_->size() - 1), selected_index_ + 1);
            ClampSelection();
            MarkDirty(rect());
        }
        return true;
    }
    if (action == Action::Confirm) {
        if (on_confirm_) {
            on_confirm_(selected_index_);
        }
        return true;
    }
    if (action == Action::Back) {
        if (on_cancel_) {
            on_cancel_();
        }
        return true;
    }
    return false;
}

void DialogWidget::Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) {
    const auto r = rect();
    if (r.IsEmpty() || !epd) return;

    gfx.fillRect(r.x, r.y, r.w, r.h, GxEPD_WHITE);
    gfx.drawRect(r.x, r.y, r.w, r.h, GxEPD_BLACK);

    const int16_t ascent = GetFontAscent(font_);
    const int16_t line_h = std::max<int16_t>(1, GetFontHeight(font_));

    int16_t y = static_cast<int16_t>(r.y + padding_ + ascent);
    if (!title_.empty()) {
        epd->DrawUtf8(static_cast<int16_t>(r.x + padding_), y, title_, font_, GxEPD_BLACK);
        y = static_cast<int16_t>(y + line_h);
    }

    std::vector<std::string> lines;
    const int16_t content_w = static_cast<int16_t>(r.w - padding_ * 2);
    WrapText(content_, content_w, epd, font_, lines);
    const int max_lines = std::max<int>(1, (r.h - padding_ * 2 - line_h * 2) / line_h);
    if (static_cast<int>(lines.size()) > max_lines) {
        lines.resize(max_lines);
        if (!lines.empty()) {
            lines.back() = TruncateToWidth(lines.back(), content_w, epd, font_, true);
        }
    }
    for (const auto& line : lines) {
        epd->DrawUtf8(static_cast<int16_t>(r.x + padding_), y, line, font_, GxEPD_BLACK);
        y = static_cast<int16_t>(y + line_h);
    }

    if (buttons_ && !buttons_->empty()) {
        const int button_count = static_cast<int>(buttons_->size());
        const int16_t button_h = static_cast<int16_t>(line_h + padding_);
        const int16_t button_y = static_cast<int16_t>(r.bottom() - padding_ - button_h);
        const int16_t button_w = static_cast<int16_t>((r.w - padding_ * 2) / button_count);
        for (int i = 0; i < button_count; ++i) {
            const int16_t bx = static_cast<int16_t>(r.x + padding_ + i * button_w);
            const bool sel = (i == selected_index_);
            if (sel) {
                gfx.fillRect(bx, button_y, button_w - 2, button_h, GxEPD_BLACK);
            } else {
                gfx.drawRect(bx, button_y, button_w - 2, button_h, GxEPD_BLACK);
            }
            const int16_t text_w = epd->MeasureUtf8Width((*buttons_)[i], font_);
            const int16_t tx = static_cast<int16_t>(bx + (button_w - text_w) / 2);
            const int16_t ty = static_cast<int16_t>(button_y + (button_h - GetFontHeight(font_)) / 2 + ascent);
            epd->DrawUtf8(tx, ty, (*buttons_)[i], font_, sel ? GxEPD_WHITE : GxEPD_BLACK);
        }
    }

    ClearDirty();
}

void DialogWidget::ClampSelection() {
    if (!buttons_ || buttons_->empty()) {
        selected_index_ = 0;
        return;
    }
    const int max_index = static_cast<int>(buttons_->size() - 1);
    if (selected_index_ < 0) selected_index_ = 0;
    if (selected_index_ > max_index) selected_index_ = max_index;
}

} // namespace eteacher::app_service::tool
