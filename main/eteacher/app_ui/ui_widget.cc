#include "eteacher/app_ui/ui_widget.h"

#include <Adafruit_GFX.h>
#include <algorithm>
#include <limits>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/font_manager/font_manager.h"

namespace eteacher::app_ui {
namespace {

// 获取字体高度（ascent + descent）。
// 参数: font_name - 内置字体名称。
// 返回: 字体总高度（像素）。如果字体不可用，返回默认高度 12。
int16_t GetFontHeight(std::string_view font_name) {
    const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
    if (!font || !font->Ready()) {
        return 12;
    }
    const auto& header = font->Header();
    return static_cast<int16_t>(header.ascent + header.descent);
}

// 获取字体的 ascent（基线以上的高度）。
// 参数: font_name - 内置字体名称。
// 返回: 字体 ascent（像素）。如果字体不可用，返回默认 ascent 9。
int16_t GetFontAscent(std::string_view font_name) {
    const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
    if (!font || !font->Ready()) {
        return 9;
    }
    return static_cast<int16_t>(font->Header().ascent);
}

// 返回 UTF-8 字符串中从位置 index 开始下一个字符的索引。
// 支持 1-4 字节的 UTF-8 编码；如果 index 超出范围，返回 text.size()。
// 参数: text - UTF-8 字符串视图，index - 当前字节索引。
// 返回: 下一个字符的起始字节索引。
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

// 按换行符分割字符串并将每行放入 out。
// 参数: text - 要分割的文本（支持多行）；out - 输出的行数组（清空后填充）。
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

// 将文本截断以适应指定宽度。
// 参数: text - 要截断的 UTF-8 文本；max_w - 最大宽度（像素）；
//        epd - 用于测量文本宽度的显示对象；font - 字体名；
//        ellipsis - 若为 true 则在末尾添加省略号 "..."（若截断）。
// 返回: 截断后的字符串（可能带省略号）。
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

// 按照像素宽度对 UTF-8 文本进行换行，结果每行放入 out。
// 参数: text - 要换行的文本；max_w - 每行最大宽度（像素）；
//        epd - 用于测量文本宽度的显示对象；font - 字体名；
//        out - 输出行数组（清空后填充）。
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

// 计算矩形 a 与 b 的交集并返回交集矩形。
// 如果交集为空，返回空矩形。
eteacher::app_ui::layout::Rect IntersectRect(const eteacher::app_ui::layout::Rect& a, const eteacher::app_ui::layout::Rect& b) {
    const int16_t x1 = std::max<int16_t>(a.x, b.x);
    const int16_t y1 = std::max<int16_t>(a.y, b.y);
    const int16_t x2 = std::min<int16_t>(a.right(), b.right());
    const int16_t y2 = std::min<int16_t>(a.bottom(), b.bottom());
    if (x2 <= x1 || y2 <= y1) return {};
    return {x1, y1, static_cast<int16_t>(x2 - x1), static_cast<int16_t>(y2 - y1)};
}

// 计算矩形 a 与 b 的并集并返回包围两者的最小矩形。
// 如果其中一个矩形为空，则返回另一个矩形。
eteacher::app_ui::layout::Rect UnionRect(const eteacher::app_ui::layout::Rect& a, const eteacher::app_ui::layout::Rect& b) {
    if (a.IsEmpty()) return b;
    if (b.IsEmpty()) return a;
    const int16_t x1 = std::min<int16_t>(a.x, b.x);
    const int16_t y1 = std::min<int16_t>(a.y, b.y);
    const int16_t x2 = std::max<int16_t>(a.right(), b.right());
    const int16_t y2 = std::max<int16_t>(a.bottom(), b.bottom());
    return {x1, y1, static_cast<int16_t>(x2 - x1), static_cast<int16_t>(y2 - y1)};
}

} // namespace

// 关联布局区域到此 Widget。
// 参数: region - 指向布局系统中该 widget 的 Region（包含位置和尺寸）。
// 关联后将标记对应区域为脏（需要重绘）。
void WidgetBase::AttachRegion(const eteacher::app_ui::layout::Region* region) {
    region_ = region;
    MarkDirty(rect());
}

// 获取当前 Widget 的矩形（位于布局中的位置和大小）。
// 返回空矩形表示未关联 Region。
eteacher::app_ui::layout::Rect WidgetBase::rect() const {
    if (!region_) return {};
    return region_->rect;
}

// 清除脏标记（表示已完成重绘），并重置脏区域。
void WidgetBase::ClearDirty() {
    dirty_ = false;
    dirty_rect_ = {};
}

// 标记 widget 为脏，需要重绘。
// 参数: rect - 要标记为脏的子矩形；若为空则标记整个 widget 的矩形。
// 脏区域会与已有脏区域合并，便于部分重绘。
void WidgetBase::MarkDirty(const eteacher::app_ui::layout::Rect& rect) {
    dirty_ = true;
    const eteacher::app_ui::layout::Rect target = rect.IsEmpty() ? this->rect() : rect;
    dirty_rect_ = UnionRect(dirty_rect_, target);
}

// 设置标签文本并标记为脏以触发重绘。
// 参数: text - 要显示的 UTF-8 文本（会移动进内部存储）。
void LabelWidget::SetText(std::string text) {
    text_ = std::move(text);
    MarkDirty(rect());
}

// 在给定的绘图上下文中绘制标签。
// 参数: gfx - 低级绘图对象；epd - 用于测量和绘制 UTF-8 文本的显示封装。
// 根据对齐、换行/截断策略绘制文本，并在完成后清除脏标记。
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

// 设置按钮文本标签并标记为脏以触发重绘。
// 参数: label - 按钮上显示的文本（移动语义）。
void ButtonWidget::SetLabel(std::string label) {
    label_ = std::move(label);
    MarkDirty(rect());
}

// 处理按钮的操作事件。
// 支持的操作: Action::Confirm（确认/按下），触发注册的回调并返回 true。
// 返回: 是否已处理该操作。
bool ButtonWidget::OnAction(Action action) {
    if (action == Action::Confirm) {
        if (on_confirm_) {
            on_confirm_();
        }
        return true;
    }
    return false;
}

// 处理焦点变化：调用基类实现并标记为脏以更新焦点样式。
// 参数: focused - 是否获得焦点。
void ButtonWidget::OnFocus(bool focused) {
    WidgetBase::OnFocus(focused);
    MarkDirty(rect());
}

// 绘制按钮：包括背景、边框和居中文本。
// 焦点状态会改变前景/背景颜色以表示选中。
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

// 设置列表项数据源并重置选择/滚动状态。
// 参数: items - 指向外部字符串向量（不复制，仅保存指针）。
void ListWidget::SetItems(const std::vector<std::string>* items) {
    items_ = items;
    selected_index_ = 0;
    top_index_ = 0;
    last_selected_ = 0;
    last_top_ = 0;
    MarkDirty(rect());
}

// 返回列表项数量（如果未设置数据源则为 0）。
int ListWidget::item_count() const {
    return items_ ? static_cast<int>(items_->size()) : 0;
}

// 设置当前选中项索引并确保在有效范围内，同时标记为脏。
// 参数: index - 目标选中索引。
void ListWidget::SetSelected(int index) {
    selected_index_ = index;
    ClampSelection();
    MarkDirty(rect());
}

// 处理上下方向键以改变列表选择。
// 支持 Action::Up 和 Action::Down，会移动选中索引、保证可见并更新脏区域。
// 返回: 是否已处理该操作。
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

// 绘制可滚动列表：仅绘制可见项并根据脏区域优化重绘。
// 支持高亮/反转显示当前选中项。
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

// 计算在当前矩形内可见的行数（基于每行高度 line_h）。
int ListWidget::VisibleCount(int16_t line_h) const {
    const auto r = rect();
    if (r.IsEmpty() || line_h <= 0) return 0;
    return std::max(1, r.h / line_h);
}

// 将 selected_index_ 限制在合法范围内，并在需要时调整 top_index_。
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

// 保证当前选中项在可见区域内，必要时调整 top_index_ 实现滚动。
void ListWidget::EnsureSelectionVisible(int16_t line_h) {
    const int visible = VisibleCount(line_h);
    if (visible <= 0) return;
    if (selected_index_ < top_index_) {
        top_index_ = selected_index_;
    } else if (selected_index_ >= top_index_ + visible) {
        top_index_ = selected_index_ - visible + 1;
    }
}

// 计算给定项在屏幕上的矩形（相对于 widget 的 rect）。
// 如果项不可见或参数无效，则返回空矩形。
eteacher::app_ui::layout::Rect ListWidget::ItemRect(int index, int16_t line_h) const {
    const auto r = rect();
    if (r.IsEmpty() || line_h <= 0) return {};
    const int local = index - top_index_;
    if (local < 0) return {};
    const int16_t y = static_cast<int16_t>(r.y + local * line_h);
    if (y >= r.bottom()) return {};
    return {r.x, y, r.w, line_h};
}

// 在选择改变时更新脏区域：
// - 如果滚动(top_index_) 发生变化，则标记整个区域为脏；
// - 否则仅合并前一次与当前选中项的矩形以做局部重绘。
void ListWidget::UpdateDirtyOnSelectionChange(int previous, int previous_top, int16_t line_h) {
    if (previous_top != top_index_) {
        MarkDirty(rect());
        return;
    }
    const auto a = ItemRect(previous, line_h);
    const auto b = ItemRect(selected_index_, line_h);
    MarkDirty(UnionRect(a, b));
}

// 设置文本输入的内容并标记为脏以触发重绘。
// 参数: text - 输入框文本（移动语义）。
void TextInputWidget::SetText(std::string text) {
    text_ = std::move(text);
    MarkDirty(rect());
}

// 在当前文本后追加 UTF-8 内容并标记为脏。
// 参数: utf8 - 要追加的 UTF-8 片段。
void TextInputWidget::AppendText(std::string_view utf8) {
    text_.append(utf8.data(), utf8.size());
    MarkDirty(rect());
}

// 执行退格操作：删除最后一个 UTF-8 字符并标记为脏。
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

// 处理输入框的操作事件。
// 支持 Action::Delete（退格），执行删除并返回 true。
bool TextInputWidget::OnAction(Action action) {
    if (action == Action::Delete) {
        Backspace();
        return true;
    }
    return false;
}

// 绘制文本输入框：边框、文本及光标（当获得焦点且光标可见时）。
// 文本超出宽度时会保留尾部可见。
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

// 设置对话框标题并标记为脏。
void DialogWidget::SetTitle(std::string title) {
    title_ = std::move(title);
    MarkDirty(rect());
}

// 设置对话框内容文本并标记为脏。
void DialogWidget::SetContent(std::string content) {
    content_ = std::move(content);
    MarkDirty(rect());
}

// 设置对话框按钮数组（指针，不复制），重置选择并标记为脏。
void DialogWidget::SetButtons(const std::vector<std::string>* buttons) {
    buttons_ = buttons;
    selected_index_ = 0;
    ClampSelection();
    MarkDirty(rect());
}

// 处理对话框的操作事件：
// - Left/Right: 切换按钮选择并标记为脏；
// - Confirm: 触发确认回调并返回 true；
// - Back: 触发取消回调并返回 true。
// 返回: 是否已处理该操作。
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

// 绘制对话框：包括边框、标题、内容文本和底部按钮区域。
// 文本会根据对话框宽度自动换行并在必要时截断显示省略号。
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

// 将对话框按钮选择索引约束到合法范围。
void DialogWidget::ClampSelection() {
    if (!buttons_ || buttons_->empty()) {
        selected_index_ = 0;
        return;
    }
    const int max_index = static_cast<int>(buttons_->size() - 1);
    if (selected_index_ < 0) selected_index_ = 0;
    if (selected_index_ > max_index) selected_index_ = max_index;
}

// 设置菜单项数据源并重置选择/滚动状态。
void MenuWidget::SetItems(const std::vector<std::string>* items) {
    items_ = items;
    selected_index_ = 0;
    top_index_ = 0;
    last_selected_ = 0;
    last_top_ = 0;
    MarkDirty(rect());
}

// 返回菜单项数量。
int MenuWidget::item_count() const {
    return items_ ? static_cast<int>(items_->size()) : 0;
}

// 设置当前选中项索引。
void MenuWidget::SetSelected(int index) {
    selected_index_ = index;
    ClampSelection();
    MarkDirty(rect());
}

// 处理菜单的操作事件：Up/Down 切换，Confirm 触发回调。
bool MenuWidget::OnAction(Action action) {
    if (item_count() <= 0) return false;

    if (action == Action::Up || action == Action::Down) {
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

    if (action == Action::Confirm) {
        if (on_confirm_) {
            on_confirm_(selected_index_);
        }
        return true;
    }
    return false;
}

// 绘制菜单列表。
void MenuWidget::Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) {
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

int MenuWidget::VisibleCount(int16_t line_h) const {
    const auto r = rect();
    if (r.IsEmpty() || line_h <= 0) return 0;
    return std::max(1, r.h / line_h);
}

void MenuWidget::ClampSelection() {
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

void MenuWidget::EnsureSelectionVisible(int16_t line_h) {
    const int visible = VisibleCount(line_h);
    if (visible <= 0) return;
    if (selected_index_ < top_index_) {
        top_index_ = selected_index_;
    } else if (selected_index_ >= top_index_ + visible) {
        top_index_ = selected_index_ - visible + 1;
    }
}

eteacher::app_ui::layout::Rect MenuWidget::ItemRect(int index, int16_t line_h) const {
    const auto r = rect();
    if (r.IsEmpty() || line_h <= 0) return {};
    const int local = index - top_index_;
    if (local < 0) return {};
    const int16_t y = static_cast<int16_t>(r.y + local * line_h);
    if (y >= r.bottom()) return {};
    return {r.x, y, r.w, line_h};
}

void MenuWidget::UpdateDirtyOnSelectionChange(int previous, int previous_top, int16_t line_h) {
    if (previous_top != top_index_) {
        MarkDirty(rect());
        return;
    }
    const auto a = ItemRect(previous, line_h);
    const auto b = ItemRect(selected_index_, line_h);
    MarkDirty(UnionRect(a, b));
}

// 设置 Tab 标签列表。
void TabViewWidget::SetTabs(const std::vector<std::string>* tabs) {
    tabs_ = tabs;
    selected_index_ = 0;
    ClampSelection();
    MarkDirty(rect());
}

// 设置 Tab 内容列表（可选）。
void TabViewWidget::SetContents(const std::vector<std::string>* contents) {
    contents_ = contents;
    MarkDirty(rect());
}

// 设置当前选中 Tab。
void TabViewWidget::SetSelected(int index) {
    selected_index_ = index;
    ClampSelection();
    MarkDirty(rect());
}

// TabView 事件处理：Left/Right 切换；Confirm 通知回调。
bool TabViewWidget::OnAction(Action action) {
    if (tab_count() <= 0) return false;
    if (action == Action::Left) {
        selected_index_ = std::max(0, selected_index_ - 1);
        ClampSelection();
        MarkDirty(rect());
        return true;
    }
    if (action == Action::Right) {
        selected_index_ = std::min(tab_count() - 1, selected_index_ + 1);
        ClampSelection();
        MarkDirty(rect());
        return true;
    }
    if (action == Action::Confirm) {
        if (on_select_) {
            on_select_(selected_index_);
        }
        return true;
    }
    return false;
}

// 绘制 TabView：顶部标签区 + 内容区。
void TabViewWidget::Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) {
    const auto r = rect();
    if (r.IsEmpty() || !epd) return;

    gfx.fillRect(r.x, r.y, r.w, r.h, GxEPD_WHITE);

    const int16_t line_h = std::max<int16_t>(1, GetFontHeight(font_));
    const int16_t ascent = GetFontAscent(font_);
    const int16_t tab_h = static_cast<int16_t>(line_h + padding_ * 2);
    const int count = tab_count();

    if (count > 0 && tab_h > 0) {
        const int16_t tab_w = static_cast<int16_t>(r.w / count);
        for (int i = 0; i < count; ++i) {
            const int16_t x = static_cast<int16_t>(r.x + i * tab_w);
            const int16_t w = (i == count - 1) ? static_cast<int16_t>(r.w - i * tab_w) : tab_w;
            const bool sel = (i == selected_index_);
            if (sel) {
                gfx.fillRect(x, r.y, w, tab_h, GxEPD_BLACK);
            } else {
                gfx.drawRect(x, r.y, w, tab_h, GxEPD_BLACK);
            }

            const std::string& title = (*tabs_)[i];
            const int16_t text_w = epd->MeasureUtf8Width(title, font_);
            const int16_t tx = static_cast<int16_t>(x + (w - text_w) / 2);
            const int16_t ty = static_cast<int16_t>(r.y + padding_ + ascent);
            epd->DrawUtf8(tx, ty, title, font_, sel ? GxEPD_WHITE : GxEPD_BLACK);
        }
    }

    if (tab_h < r.h) {
        const eteacher::app_ui::layout::Rect content_rect{r.x, static_cast<int16_t>(r.y + tab_h), r.w,
                                                          static_cast<int16_t>(r.h - tab_h)};
        gfx.drawRect(content_rect.x, content_rect.y, content_rect.w, content_rect.h, GxEPD_BLACK);

        const auto content = SelectedContent();
        if (!content.empty()) {
            const int16_t content_w = static_cast<int16_t>(content_rect.w - padding_ * 2);
            const int16_t content_h = static_cast<int16_t>(content_rect.h - padding_ * 2);
            const int max_lines = std::max<int>(1, content_h / line_h);

            std::vector<std::string> lines;
            WrapText(content, content_w, epd, font_, lines);
            if (static_cast<int>(lines.size()) > max_lines) {
                lines.resize(max_lines);
                if (!lines.empty()) {
                    lines.back() = TruncateToWidth(lines.back(), content_w, epd, font_, true);
                }
            }
            int16_t y = static_cast<int16_t>(content_rect.y + padding_ + ascent);
            for (const auto& line : lines) {
                epd->DrawUtf8(static_cast<int16_t>(content_rect.x + padding_), y, line, font_, GxEPD_BLACK);
                y = static_cast<int16_t>(y + line_h);
            }
        }
    }

    ClearDirty();
}

int TabViewWidget::tab_count() const {
    return tabs_ ? static_cast<int>(tabs_->size()) : 0;
}

void TabViewWidget::ClampSelection() {
    const int count = tab_count();
    if (count <= 0) {
        selected_index_ = 0;
        return;
    }
    selected_index_ = std::max(0, std::min(selected_index_, count - 1));
}

std::string_view TabViewWidget::SelectedContent() const {
    if (!contents_ || selected_index_ < 0) return {};
    if (selected_index_ >= static_cast<int>(contents_->size())) return {};
    return (*contents_)[selected_index_];
}

// 设置复选框文本。
void CheckboxWidget::SetLabel(std::string label) {
    label_ = std::move(label);
    MarkDirty(rect());
}

// 设置复选框选中状态。
void CheckboxWidget::SetChecked(bool checked) {
    checked_ = checked;
    MarkDirty(rect());
}

// Confirm 切换复选框。
bool CheckboxWidget::OnAction(Action action) {
    if (action == Action::Confirm) {
        checked_ = !checked_;
        if (on_toggle_) {
            on_toggle_(checked_);
        }
        MarkDirty(rect());
        return true;
    }
    return false;
}

void CheckboxWidget::OnFocus(bool focused) {
    WidgetBase::OnFocus(focused);
    MarkDirty(rect());
}

// 绘制复选框。
void CheckboxWidget::Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) {
    const auto r = rect();
    if (r.IsEmpty() || !epd) return;

    gfx.fillRect(r.x, r.y, r.w, r.h, GxEPD_WHITE);

    const int16_t line_h = std::max<int16_t>(1, GetFontHeight(font_));
    const int16_t ascent = GetFontAscent(font_);
    const int16_t box = static_cast<int16_t>(std::min<int16_t>(line_h, r.h - padding_ * 2));
    const int16_t box_x = static_cast<int16_t>(r.x + padding_);
    const int16_t box_y = static_cast<int16_t>(r.y + (r.h - box) / 2);

    gfx.drawRect(box_x, box_y, box, box, GxEPD_BLACK);
    if (checked_) {
        gfx.drawLine(box_x + 2, box_y + box / 2, box_x + box / 2, box_y + box - 3, GxEPD_BLACK);
        gfx.drawLine(box_x + box / 2, box_y + box - 3, box_x + box - 2, box_y + 2, GxEPD_BLACK);
    }

    const int16_t text_x = static_cast<int16_t>(box_x + box + padding_);
    const int16_t text_y = static_cast<int16_t>(r.y + (r.h - GetFontHeight(font_)) / 2 + ascent);
    epd->DrawUtf8(text_x, text_y, label_, font_, GxEPD_BLACK);

    if (focused()) {
        gfx.drawRect(r.x, r.y, r.w, r.h, GxEPD_BLACK);
    }

    ClearDirty();
}

// 设置单选列表数据源。
void RadioGroupWidget::SetItems(const std::vector<std::string>* items) {
    items_ = items;
    selected_index_ = 0;
    top_index_ = 0;
    last_selected_ = 0;
    last_top_ = 0;
    MarkDirty(rect());
}

// 返回单选项数量。
int RadioGroupWidget::item_count() const {
    return items_ ? static_cast<int>(items_->size()) : 0;
}

// 设置选中项。
void RadioGroupWidget::SetSelected(int index) {
    selected_index_ = index;
    ClampSelection();
    MarkDirty(rect());
}

// 处理单选列表事件。
bool RadioGroupWidget::OnAction(Action action) {
    if (item_count() <= 0) return false;

    if (action == Action::Up || action == Action::Down) {
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

    if (action == Action::Confirm) {
        if (on_confirm_) {
            on_confirm_(selected_index_);
        }
        return true;
    }
    return false;
}

// 绘制单选列表。
void RadioGroupWidget::Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) {
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

    const int16_t ascent = GetFontAscent(font_);
    const int16_t radius = static_cast<int16_t>(std::max<int16_t>(2, line_h / 4));

    for (int idx = top_index_; idx < end; ++idx) {
        const auto item_rect = ItemRect(idx, line_h);
        if (draw_area.IsEmpty() || IntersectRect(item_rect, draw_area).IsEmpty()) {
            if (!draw_area.IsEmpty()) continue;
        }

        const bool selected = (idx == selected_index_);
        const int16_t cx = static_cast<int16_t>(item_rect.x + padding_ + radius);
        const int16_t cy = static_cast<int16_t>(item_rect.y + line_h / 2);
        gfx.drawCircle(cx, cy, radius, GxEPD_BLACK);
        if (selected) {
            gfx.fillCircle(cx, cy, static_cast<int16_t>(radius - 1), GxEPD_BLACK);
        }

        const std::string& text = (*items_)[idx];
        const int16_t text_x = static_cast<int16_t>(item_rect.x + padding_ + radius * 2 + padding_);
        const int16_t text_y = static_cast<int16_t>(item_rect.y + ascent + (line_h - GetFontHeight(font_)) / 2);
        epd->DrawUtf8(text_x, text_y, text, font_, GxEPD_BLACK);
    }

    last_selected_ = selected_index_;
    last_top_ = top_index_;
    ClearDirty();
}

int RadioGroupWidget::VisibleCount(int16_t line_h) const {
    const auto r = rect();
    if (r.IsEmpty() || line_h <= 0) return 0;
    return std::max(1, r.h / line_h);
}

void RadioGroupWidget::ClampSelection() {
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

void RadioGroupWidget::EnsureSelectionVisible(int16_t line_h) {
    const int visible = VisibleCount(line_h);
    if (visible <= 0) return;
    if (selected_index_ < top_index_) {
        top_index_ = selected_index_;
    } else if (selected_index_ >= top_index_ + visible) {
        top_index_ = selected_index_ - visible + 1;
    }
}

eteacher::app_ui::layout::Rect RadioGroupWidget::ItemRect(int index, int16_t line_h) const {
    const auto r = rect();
    if (r.IsEmpty() || line_h <= 0) return {};
    const int local = index - top_index_;
    if (local < 0) return {};
    const int16_t y = static_cast<int16_t>(r.y + local * line_h);
    if (y >= r.bottom()) return {};
    return {r.x, y, r.w, line_h};
}

void RadioGroupWidget::UpdateDirtyOnSelectionChange(int previous, int previous_top, int16_t line_h) {
    if (previous_top != top_index_) {
        MarkDirty(rect());
        return;
    }
    const auto a = ItemRect(previous, line_h);
    const auto b = ItemRect(selected_index_, line_h);
    MarkDirty(UnionRect(a, b));
}

} // namespace eteacher::app_ui
