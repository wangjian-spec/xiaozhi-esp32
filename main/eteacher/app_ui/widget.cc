#include "widget.h"

#include "renderer.h"
#include "input.h"
#include "ui_engine.h"
#include "eteacher/app_ui/status_bar.h"
#include "boards/common/board.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/font_manager/font_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include "debug.h"
namespace app_ui {

namespace {

constexpr int kKeyboardRows = 4;
constexpr int kKeyboardCols = 9;
constexpr int kKeyboardPageSize = kKeyboardRows * kKeyboardCols;
constexpr int kKeyboardPages = 3;

const char* kKeyboardPage1[kKeyboardPageSize] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i",
    "j", "k", "l", "m", "n", "o", "p", "q", "r",
    "s", "t", "u", "v", "w", "x", "y", "z", "0",
    "1", "2", "3", "4", "5", "6", "7", "8", "9"
};

const char* kKeyboardPage2[kKeyboardPageSize] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I",
    "J", "K", "L", "M", "N", "O", "P", "Q", "R",
    "S", "T", "U", "V", "W", "X", "Y", "Z", "!",
    "\"", "#", "$", "%", "&", "'", "(", ")", "*"
};

const char* kKeyboardPage3[kKeyboardPageSize] = {
    "+", ",", "-", ".", "/", ":", ";", "<", "=",
    ">", "?", "@", "[", "\\", "]", "^", "_", "`",
    "{", "|", "}", "~", "空格", "", "", "", "",
    "", "", "", "", "", "", "", "", ""
};

bool IsKeyDownOrRepeat(const InputEvent& e) {
    return e.type == InputType::KeyDown || e.type == InputType::KeyRepeat;
}

bool IsActivationKey(KeyCode key) {
    return key == KeyCode::Start || key == KeyCode::Select;
}

int ClampIndexByCount(int index, int count) {
    if (count <= 0) {
        return 0;
    }
    if (index < 0) {
        return 0;
    }
    const int max_index = count - 1;
    if (index > max_index) {
        return max_index;
    }
    return index;
}

void DrawCenteredTextInCell(Painter& p, int x, int y, int w, int h, const char* label) {
    if (!label || !label[0] || w <= 0 || h <= 0) {
        return;
    }

    const Size text_size = p.MeasureText(label, nullptr);
    int16_t tx = static_cast<int16_t>(x + (w - text_size.w) / 2);
    int16_t ty = static_cast<int16_t>(y + (h - text_size.h) / 2);
    if (text_size.w > w) {
        tx = static_cast<int16_t>(x + 2);
    }
    if (text_size.h > h) {
        ty = static_cast<int16_t>(y + 2);
    }
    p.DrawText({tx, ty}, label);
}

int ResolveGridCols(int item_count, int rows_hint, int cols_hint) {
    const int rows = std::max(1, rows_hint);
    if (cols_hint > 0) {
        return cols_hint;
    }
    return std::max(1, (item_count + rows - 1) / rows);
}

struct GridLayoutMetrics {
    int rows = 1;
    int cols = 1;
    int cell_w = 1;
    int cell_h = 1;
};

GridLayoutMetrics MakeGridLayoutMetrics(const Rect& rect, int rows_hint, int cols_hint) {
    GridLayoutMetrics metrics;
    metrics.rows = std::max(1, rows_hint);
    metrics.cols = std::max(1, cols_hint);
    metrics.cell_w = std::max(1, rect.w / metrics.cols);
    metrics.cell_h = std::max(1, rect.h / metrics.rows);
    return metrics;
}

void DrawGridOutline(Painter& p, const Rect& rect, const GridLayoutMetrics& metrics) {
    p.SetDrawColor(Color::Black);
    p.DrawRect(rect);
    for (int r = 1; r < metrics.rows; ++r) {
        p.DrawHLine({0, static_cast<int16_t>(r * metrics.cell_h)}, rect.w);
    }
    for (int c = 1; c < metrics.cols; ++c) {
        p.DrawVLine({static_cast<int16_t>(c * metrics.cell_w), 0}, rect.h);
    }
}

void DrawSimpleBarFallback(Painter& p, const Rect& rect, const std::string& text) {
    p.SetDrawColor(Color::Black);
    p.FillRect(rect);
    p.SetTextColor(Color::White);
    if (!text.empty()) {
        p.DrawText({2, 2}, text.c_str());
    }
}

uint8_t ClampProgressValue(uint8_t value, uint8_t max_value) {
    if (max_value == 0) {
        return 0;
    }
    return value > max_value ? max_value : value;
}

std::string TrimCopy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

void ExtractQuotedItems(const std::string& text,
                        const std::string& open,
                        const std::string& close,
                        std::vector<std::string>& out) {
    size_t pos = 0;
    while (true) {
        size_t start = text.find(open, pos);
        if (start == std::string::npos) {
            break;
        }
        start += open.size();
        size_t end = text.find(close, start);
        if (end == std::string::npos) {
            break;
        }
        std::string item = TrimCopy(text.substr(start, end - start));
        if (!item.empty()) {
            out.push_back(std::move(item));
        }
        pos = end + close.size();
    }
}

std::vector<std::string> ParseItemsFromText(const std::string& text) {
    std::vector<std::string> items;
    if (text.empty()) {
        return items;
    }

    ExtractQuotedItems(text, "\"", "\"", items);
    ExtractQuotedItems(text, "\xE2\x80\x9C", "\xE2\x80\x9D", items);

    if (!items.empty()) {
        return items;
    }

    std::string current;
    for (char c : text) {
        if (c == '\n' || c == '|' || c == ',' || c == ';') {
            std::string item = TrimCopy(current);
            if (!item.empty()) {
                items.push_back(std::move(item));
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    std::string item = TrimCopy(current);
    if (!item.empty()) {
        items.push_back(std::move(item));
    }
    return items;
}

void SplitListItemText(const char* text, std::string& left, std::string& right) {
    left.clear();
    right.clear();
    if (!text) {
        return;
    }
    const char* delimiter = std::strchr(text, '\t');
    if (!delimiter) {
        left = text;
        return;
    }
    left.assign(text, static_cast<size_t>(delimiter - text));
    right.assign(delimiter + 1);
}

enum class ListMarkerStyle : uint8_t {
    None,
    Todo,
    Done,
    Deleted,
};

bool ConsumeListMarkerPrefix(std::string& text, ListMarkerStyle& style) {
    struct PrefixMap {
        const char* prefix;
        ListMarkerStyle style;
    };
    static constexpr PrefixMap kPrefixMap[] = {
        {"[[TODO]]", ListMarkerStyle::Todo},
        {"[[DONE]]", ListMarkerStyle::Done},
        {"[[DELETED]]", ListMarkerStyle::Deleted},
    };

    for (const auto& item : kPrefixMap) {
        const size_t len = std::strlen(item.prefix);
        if (text.size() < len) {
            continue;
        }
        if (text.compare(0, len, item.prefix) == 0) {
            style = item.style;
            text.erase(0, len);
            while (!text.empty() && text.front() == ' ') {
                text.erase(0, 1);
            }
            return true;
        }
    }
    style = ListMarkerStyle::None;
    return false;
}

void DrawListMarker(Painter& p, int y, int h, ListMarkerStyle style, bool selected) {
    if (style == ListMarkerStyle::None) {
        return;
    }

    p.SetDrawColor(selected ? Color::White : Color::Black);
    constexpr int kMarkerDiameter = 16;
    const int radius = kMarkerDiameter / 2;
    const int cx = 2 + radius;
    const int cy = y + h / 2;
    p.DrawCircle({static_cast<int16_t>(cx), static_cast<int16_t>(cy)}, static_cast<int16_t>(radius));

    if (style == ListMarkerStyle::Deleted) {
        const int x1 = cx - radius + 2;
        const int y1 = cy - radius + 2;
        const int x2 = cx + radius - 2;
        const int y2 = cy + radius - 2;
        const int len = std::max(0, x2 - x1);
        for (int i = 0; i <= len; ++i) {
            p.FillRect({static_cast<int16_t>(x1 + i), static_cast<int16_t>(y1 + i), 1, 1});
            p.FillRect({static_cast<int16_t>(x1 + i), static_cast<int16_t>(y2 - i), 1, 1});
        }
    } else if (style == ListMarkerStyle::Done) {
        const int x1 = cx - radius + 2;
        const int y1 = cy;
        const int x2 = cx - 1;
        const int y2 = cy + radius - 2;
        const int x3 = cx + radius - 2;
        for (int i = 0; i <= std::max(0, x2 - x1); ++i) {
            p.FillRect({static_cast<int16_t>(x1 + i), static_cast<int16_t>(y1 + i), 1, 1});
        }
        for (int i = 0; i <= std::max(0, x3 - x2); ++i) {
            p.FillRect({static_cast<int16_t>(x2 + i), static_cast<int16_t>(y2 - i), 1, 1});
        }
    }
}

int Utf8CharLen(unsigned char c) {
    if ((c & 0x80u) == 0) {
        return 1;
    }
    if ((c & 0xE0u) == 0xC0u) {
        return 2;
    }
    if ((c & 0xF0u) == 0xE0u) {
        return 3;
    }
    if ((c & 0xF8u) == 0xF0u) {
        return 4;
    }
    return 1;
}

std::vector<std::string> WrapTextByWidth(Painter& p, const std::string& text, int max_width, int max_lines) {
    std::vector<std::string> lines;
    if (text.empty() || max_width <= 0 || max_lines <= 0) {
        return lines;
    }

    const Size full_size = p.MeasureText(text.c_str(), nullptr);
    if (full_size.w <= max_width) {
        lines.push_back(text);
        return lines;
    }

    size_t pos = 0;
    while (pos < text.size() && static_cast<int>(lines.size()) < max_lines) {
        std::string line;
        size_t last = pos;
        while (last < text.size()) {
            const int ch_len = Utf8CharLen(static_cast<unsigned char>(text[last]));
            const size_t next = std::min(text.size(), last + static_cast<size_t>(ch_len));
            std::string trial = line + text.substr(last, next - last);
            const Size s = p.MeasureText(trial.c_str(), nullptr);
            if (s.w > max_width && !line.empty()) {
                break;
            }
            line.swap(trial);
            last = next;
            if (s.w > max_width) {
                break;
            }
        }

        if (line.empty()) {
            break;
        }
        lines.push_back(std::move(line));
        pos = last;
    }

    return lines;
}

class StaticVectorModel : public ItemModel {
public:
    explicit StaticVectorModel(std::vector<std::string> items)
        : items_(std::move(items)) {}

    int Count() const override {
        return static_cast<int>(items_.size());
    }

    const char* Label(int index) const override {
        if (index < 0 || index >= static_cast<int>(items_.size())) {
            return "";
        }
        return items_[index].c_str();
    }

private:
    std::vector<std::string> items_{};
};

class TextParsedModel : public ItemModel {
public:
    explicit TextParsedModel(const std::string& text)
        : items_(ParseItemsFromText(text)) {}

    int Count() const override {
        return static_cast<int>(items_.size());
    }

    const char* Label(int index) const override {
        if (index < 0 || index >= static_cast<int>(items_.size())) {
            return "";
        }
        return items_[index].c_str();
    }

private:
    std::vector<std::string> items_{};
};

class DefaultListViewBehavior : public ListViewBehavior {
public:
    InputResult OnInput(ListViewWidget& widget, const InputEvent& e, InputPhase phase) override {
        if (phase == InputPhase::Capture) {
            return InputResult::Continue;
        }
        if (!widget.Enabled() || !widget.Visible()) {
            return InputResult::Continue;
        }
        if (!IsKeyDownOrRepeat(e)) {
            return InputResult::Continue;
        }
        if (!widget.Profile().activation_enabled) {
            return InputResult::Continue;
        }

        widget.ClampSelection();
        if (widget.ItemCount() <= 0) {
            return InputResult::Continue;
        }

        const KeyCode key = static_cast<KeyCode>(e.key);
        if (IsActivationKey(key)) {
            widget.ActivateSelected();
            return InputResult::Consume;
        }
        return InputResult::Continue;
    }

    FocusIntent OnFocusKey(ListViewWidget& widget, KeyCode key) override {
        widget.ClampSelection();
        const int count = widget.ItemCount();
        if (count <= 0) {
            return FocusIntent::Bubble;
        }
        if (!widget.Profile().selection_enabled) {
            return FocusIntent::Bubble;
        }

        const int cols = std::max(1, widget.Profile().cols);
        if (cols <= 1) {
            if (key == KeyCode::Up) {
                if (widget.SelectedIndex() > 0) {
                    widget.SetSelectedIndex(widget.SelectedIndex() - 1);
                    return FocusIntent::Consume;
                }
                return FocusIntent::EscapeUp;
            }
            if (key == KeyCode::Down) {
                if (widget.SelectedIndex() + 1 < count) {
                    widget.SetSelectedIndex(widget.SelectedIndex() + 1);
                    return FocusIntent::Consume;
                }
                return FocusIntent::EscapeDown;
            }

            const int page_rows = std::max(1, widget.Profile().rows > 0 ? widget.Profile().rows : count);
            if (key == KeyCode::Left) {
                const int old = widget.SelectedIndex();
                widget.SetSelectedIndex(std::max(0, old - page_rows));
                if (widget.SelectedIndex() != old) {
                    return FocusIntent::Consume;
                }
                return FocusIntent::EscapeLeft;
            }
            if (key == KeyCode::Right) {
                const int old = widget.SelectedIndex();
                widget.SetSelectedIndex(std::min(count - 1, old + page_rows));
                if (widget.SelectedIndex() != old) {
                    return FocusIntent::Consume;
                }
                return FocusIntent::EscapeRight;
            }
            return FocusIntent::None;
        }

        const int index = widget.SelectedIndex();
        const int row = index / cols;
        const int col = index % cols;
        if (key == KeyCode::Left) {
            if (col > 0) {
                widget.SetSelectedIndex(index - 1);
                return FocusIntent::Consume;
            }
            return FocusIntent::EscapeLeft;
        }
        if (key == KeyCode::Right) {
            if (index + 1 < count && col + 1 < cols) {
                widget.SetSelectedIndex(index + 1);
                return FocusIntent::Consume;
            }
            return FocusIntent::EscapeRight;
        }
        if (key == KeyCode::Up) {
            if (row > 0) {
                widget.SetSelectedIndex(index - cols);
                return FocusIntent::Consume;
            }
            return FocusIntent::EscapeUp;
        }
        if (key == KeyCode::Down) {
            if (index + cols < count) {
                widget.SetSelectedIndex(index + cols);
                return FocusIntent::Consume;
            }
            return FocusIntent::EscapeDown;
        }
        return FocusIntent::None;
    }

    void OnDraw(ListViewWidget& widget, Painter& p) override {
        widget.ClampSelection();
        Rect rect = widget.RectInParent();
        rect.x = 0;
        rect.y = 0;
        if (rect.w <= 0 || rect.h <= 0) {
            return;
        }

        const int cols = std::max(1, widget.Profile().cols);
        if (cols <= 1) {
            DrawLinear(widget, p, rect);
            return;
        }
        DrawGrid(widget, p, rect);
    }

private:
    void DrawLinear(ListViewWidget& widget, Painter& p, const Rect& rect) {
        const int item_count = widget.ItemCount();
        const int rows = std::max(1, widget.Profile().rows > 0 ? widget.Profile().rows : item_count);
        int start_index = 0;
        if (item_count > rows) {
            start_index = widget.SelectedIndex() - rows / 2;
            if (start_index < 0) {
                start_index = 0;
            }
            const int max_start = item_count - rows;
            if (start_index > max_start) {
                start_index = max_start;
            }
        }

        const auto& profile = widget.Profile();
        const bool focused = profile.selection_enabled && profile.focus_highlight_enabled && widget.Focused();
        const int line_h = std::max<int>(1, p.MeasureText("A", nullptr).h);
        int y = 0;
        int item_index = start_index;
        while (item_index < item_count && y < rect.h) {
            const bool selected = focused && (item_index == widget.SelectedIndex());
            const char* label = widget.ItemLabel(item_index);
            std::string left_text;
            std::string right_text;
            if (label && label[0]) {
                SplitListItemText(label, left_text, right_text);
            }

            ListMarkerStyle marker_style = ListMarkerStyle::None;
            ConsumeListMarkerPrefix(left_text, marker_style);
            const int marker_w = (marker_style == ListMarkerStyle::None) ? 0 : 31;
            const int text_x = 2 + marker_w;
            const int text_w = std::max(1, rect.w - text_x - 2);

            const auto lines = left_text.empty() ? std::vector<std::string>{}
                                                 : WrapTextByWidth(p, left_text, text_w, std::max(1, item_count));
            const int text_lines = std::max<int>(1, static_cast<int>(lines.size()));
            int h = 4 + text_lines * line_h;
            if (h < 20) {
                h = 20;
            }
            if (y + h > rect.h) {
                break;
            }

            p.SetDrawColor(selected ? Color::Black : Color::White);
            p.FillRect({0, static_cast<int16_t>(y), rect.w, static_cast<int16_t>(h)});
            p.SetTextColor(selected ? Color::White : Color::Black);

            if (marker_style != ListMarkerStyle::None) {
                DrawListMarker(p, y, h, marker_style, selected);
            }
            if (!left_text.empty()) {
                for (size_t li = 0; li < lines.size(); ++li) {
                    const int line_y = y + 2 + static_cast<int>(li) * line_h;
                    if (line_y + line_h > y + h) {
                        break;
                    }
                    p.DrawText({static_cast<int16_t>(text_x), static_cast<int16_t>(line_y)}, lines[li].c_str());
                }
            }
            if (!right_text.empty()) {
                const Size right_size = p.MeasureText(right_text.c_str(), nullptr);
                int16_t right_x = static_cast<int16_t>(rect.w - right_size.w - 2);
                if (right_x < 2) {
                    right_x = 2;
                }
                p.DrawText({right_x, static_cast<int16_t>(y + 2)}, right_text.c_str());
            }

            y += h;
            ++item_index;
        }

        p.SetDrawColor(Color::Black);
        p.DrawRect(rect);
    }

    void DrawGrid(ListViewWidget& widget, Painter& p, const Rect& rect) {
        const int item_count = widget.ItemCount();
        const GridLayoutMetrics metrics = MakeGridLayoutMetrics(rect, widget.Profile().rows, widget.Profile().cols);
        const int page_size = metrics.rows * metrics.cols;
        const auto& profile = widget.Profile();
        const bool focused = profile.selection_enabled && profile.focus_highlight_enabled && widget.Focused();

        int start_index = 0;
        if (item_count > page_size && widget.Profile().selection_enabled) {
            start_index = (widget.SelectedIndex() / page_size) * page_size;
        }

        for (int r = 0; r < metrics.rows; ++r) {
            const int y = r * metrics.cell_h;
            for (int c = 0; c < metrics.cols; ++c) {
                const int index = start_index + r * metrics.cols + c;
                const int x = c * metrics.cell_w;
                const int w = (c == metrics.cols - 1) ? (rect.w - x) : metrics.cell_w;
                const int h = (r == metrics.rows - 1) ? (rect.h - y) : metrics.cell_h;
                if (w <= 0 || h <= 0) {
                    continue;
                }

                const bool selected = focused && (index == widget.SelectedIndex());
                p.SetDrawColor(selected ? Color::Black : Color::White);
                p.FillRect({static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)});
                p.SetTextColor(selected ? Color::White : Color::Black);

                if (index < item_count) {
                    const char* label = widget.ItemLabel(index);
                    DrawCenteredTextInCell(p, x, y, w, h, label);
                }
            }
        }
        DrawGridOutline(p, rect, metrics);
    }
};

class DefaultDialogBehavior : public DialogBehavior {
public:
    InputResult OnInput(DialogWidget& widget, const InputEvent& e, InputPhase phase) override {
        if (phase == InputPhase::Capture) {
            return InputResult::Continue;
        }
        if (!widget.Enabled() || !widget.Visible()) {
            return InputResult::Continue;
        }
        if (!IsKeyDownOrRepeat(e)) {
            return InputResult::Continue;
        }

        const auto& profile = widget.Profile();
        if (profile.mode == DialogProfile::Mode::PlainText && !profile.navigation_enabled) {
            return InputResult::Continue;
        }

        const KeyCode key = static_cast<KeyCode>(e.key);
        if (IsActivationKey(key) || key == KeyCode::C) {
            widget.NotifySelected();
            return InputResult::Consume;
        }

        if (!profile.navigation_enabled) {
            return InputResult::Continue;
        }

        if (profile.mode == DialogProfile::Mode::Prompt) {
            if (key == KeyCode::Left) {
                widget.SetSelectedIndex(0);
                return InputResult::Consume;
            }
            if (key == KeyCode::Right) {
                widget.SetSelectedIndex(1);
                return InputResult::Consume;
            }
            return InputResult::Continue;
        }

        const auto& items = widget.Items();
        const int count = static_cast<int>(items.size());
        if (count <= 0) {
            return InputResult::Continue;
        }

        const int cols = ResolveGridCols(count, profile.grid_rows, profile.grid_cols);
        if (key == KeyCode::Left) {
            widget.SetSelectedIndex(std::max(0, widget.SelectedIndex() - 1));
            return InputResult::Consume;
        }
        if (key == KeyCode::Right) {
            widget.SetSelectedIndex(std::min(count - 1, widget.SelectedIndex() + 1));
            return InputResult::Consume;
        }
        if (key == KeyCode::Up) {
            widget.SetSelectedIndex(std::max(0, widget.SelectedIndex() - cols));
            return InputResult::Consume;
        }
        if (key == KeyCode::Down) {
            widget.SetSelectedIndex(std::min(count - 1, widget.SelectedIndex() + cols));
            return InputResult::Consume;
        }

        return InputResult::Continue;
    }

    void OnDraw(DialogWidget& widget, Painter& p) override {
        Rect rect = widget.RectInParent();
        rect.x = 0;
        rect.y = 0;
        p.SetDrawColor(Color::White);
        p.FillRect(rect);
        p.SetDrawColor(Color::Black);
        p.SetTextColor(Color::Black);

        const auto& profile = widget.Profile();
        if (profile.mode == DialogProfile::Mode::Prompt) {
            DrawPrompt(widget, p, rect);
            return;
        }
        if (profile.mode == DialogProfile::Mode::Grid) {
            DrawGrid(widget, p, rect, widget.Items(), widget.SelectedIndex(), profile.grid_rows, profile.grid_cols,
                     profile.selection_highlight_enabled);
            return;
        }

        p.DrawRect(rect);
        if (!widget.Text().empty()) {
            p.DrawText({2, 2}, widget.Text().c_str());
        }
    }

private:
    void DrawPrompt(DialogWidget& widget, Painter& p, const Rect& rect) {
        p.DrawRect(rect);

        const int button_h = 24;
        const int content_h = std::max(1, rect.h - button_h - 2);
        const int max_text_w = std::max(1, rect.w - 4);
        const auto lines = WrapTextByWidth(p, widget.Text(), max_text_w, 8);
        const int line_h = std::max<int>(1, p.MeasureText("A", nullptr).h);
        int y = 2;
        for (const auto& line : lines) {
            if (y + line_h > content_h) {
                break;
            }
            p.SetTextColor(Color::Black);
            p.DrawText({2, static_cast<int16_t>(y)}, line.c_str());
            y += line_h;
        }

        const int btn_y = rect.h - button_h;
        const int btn_w = rect.w / 2;
        const bool left_selected = widget.SelectedIndex() <= 0;
        DrawPromptButton(p, 0, btn_y, btn_w, button_h, widget.Profile().confirm_label.c_str(), left_selected,
                         widget.Profile().selection_highlight_enabled);
        DrawPromptButton(p, btn_w, btn_y, rect.w - btn_w, button_h, widget.Profile().cancel_label.c_str(),
                         !left_selected, widget.Profile().selection_highlight_enabled);
        p.DrawHLine({0, static_cast<int16_t>(btn_y)}, rect.w);
        p.DrawVLine({static_cast<int16_t>(btn_w), static_cast<int16_t>(btn_y)}, button_h);
    }

    void DrawPromptButton(Painter& p,
                          int x,
                          int y,
                          int w,
                          int h,
                          const char* label,
                          bool selected,
                          bool highlight_enabled) {
        const bool highlight = selected && highlight_enabled;
        p.SetDrawColor(highlight ? Color::Black : Color::White);
        p.FillRect({static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)});
        p.SetTextColor(highlight ? Color::White : Color::Black);
        DrawCenteredTextInCell(p, x, y, w, h, label);
    }

    void DrawGrid(DialogWidget& widget,
                  Painter& p,
                  const Rect& rect,
                  const std::vector<std::string>& items,
                  int selected_index,
                  int rows_hint,
                  int cols_hint,
                  bool highlight_enabled) {
        if (items.empty()) {
            p.DrawRect(rect);
            return;
        }

        const int rows = std::max(1, rows_hint);
        const int cols = ResolveGridCols(static_cast<int>(items.size()), rows_hint, cols_hint);
        const GridLayoutMetrics metrics = MakeGridLayoutMetrics(rect, rows, cols);

        for (int r = 0; r < metrics.rows; ++r) {
            const int y = r * metrics.cell_h;
            for (int c = 0; c < metrics.cols; ++c) {
                const int index = r * metrics.cols + c;
                const int x = c * metrics.cell_w;
                const int w = (c == metrics.cols - 1) ? (rect.w - x) : metrics.cell_w;
                const int h = (r == metrics.rows - 1) ? (rect.h - y) : metrics.cell_h;
                if (w <= 0 || h <= 0) {
                    continue;
                }

                const bool selected = highlight_enabled && (index == selected_index);
                p.SetDrawColor(selected ? Color::Black : Color::White);
                p.FillRect({static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)});
                p.SetTextColor(selected ? Color::White : Color::Black);

                if (index < static_cast<int>(items.size())) {
                    const char* label = items[static_cast<size_t>(index)].c_str();
                    DrawCenteredTextInCell(p, x, y, w, h, label);
                }
            }
        }

        DrawGridOutline(p, rect, metrics);
        (void)widget;
    }
};

ListViewBehavior& DefaultListViewBehaviorInstance() {
    static DefaultListViewBehavior instance;
    return instance;
}

DialogBehavior& DefaultDialogBehaviorInstance() {
    static DefaultDialogBehavior instance;
    return instance;
}

} // namespace

Widget* Widget::AddChild(std::unique_ptr<Widget> child) {
    if (!child) {
        return nullptr;
    }
    child->parent_ = this;
    child->SetEngine(engine_);
    Widget* raw = child.get();
    children_.push_back(std::move(child));
    MarkLayoutDirty();
    if (engine_) {
        engine_->MarkFocusDirty();
    }
    return raw;
}

Widget* Widget::Parent() const {
    return parent_;
}

const std::vector<std::unique_ptr<Widget>>& Widget::Children() const {
    return children_;
}

Size Widget::Measure(const Size& constraint) {
    if ((dirty_bits_ & DirtyMeasure) == 0 && cache_.measure_valid && cache_.last_constraint == constraint) {
        return cache_.measured;
    }
    cache_.measured = OnMeasure(constraint);
    cache_.last_constraint = constraint;
    cache_.measure_valid = true;
    dirty_bits_ &= static_cast<uint16_t>(~DirtyMeasure);
    cache_.measure_version++;
    return cache_.measured;
}

void Widget::Layout(const Rect& rect) {
    const Rect old_rect = RectInWindow();
    cache_.layout = rect;
    cache_.layout_valid = true;
    cache_.layout_version++;
    if (parent_) {
        cache_.global_rect = rect.Offset({parent_->cache_.global_rect.x, parent_->cache_.global_rect.y});
    } else {
        cache_.global_rect = rect;
    }
    dirty_bits_ &= static_cast<uint16_t>(~DirtyLayout);
    dirty_bits_ &= static_cast<uint16_t>(~DirtyMeasure);
    OnLayout(rect);

    const Rect new_rect = RectInWindow();
    if (old_rect.x != new_rect.x || old_rect.y != new_rect.y || old_rect.w != new_rect.w || old_rect.h != new_rect.h) {
        dirty_bits_ |= DirtyVisual;
        if (engine_) {
            engine_->AddDirty(old_rect, DirtyReason::Layout);
            engine_->AddDirty(new_rect, DirtyReason::Layout);
        }
    }
}

void Widget::Draw(Painter& p) {
    if (!Visible()) {
        return;
    }
    p.SetFont(&font_);
    OnDraw(p);
    dirty_bits_ &= static_cast<uint16_t>(~DirtyVisual);
}

void Widget::Draw(Painter& p, const Rect& dirty) {
    if (!Visible()) {
        return;
    }
    p.SetFont(&font_);
    OnDraw(p, dirty);
    dirty_bits_ &= static_cast<uint16_t>(~DirtyVisual);
}
void Widget::OnDraw(Painter& p, const Rect&) {
    OnDraw(p);
}
void Widget::SetId(uint32_t id) {
    id_ = id;
}

uint32_t Widget::Id() const {
    return id_;
}

Widget* Widget::FindById(uint32_t id) {
    if (id_ == id) {
        return this;
    }
    for (const auto& child : children_) {
        if (!child) {
            continue;
        }
        Widget* found = child->FindById(id);
        if (found) {
            return found;
        }
    }
    return nullptr;
}

const Widget* Widget::FindById(uint32_t id) const {
    if (id_ == id) {
        return this;
    }
    for (const auto& child : children_) {
        if (!child) {
            continue;
        }
        const Widget* found = child->FindById(id);
        if (found) {
            return found;
        }
    }
    return nullptr;
}

void Widget::SetLayoutMode(LayoutMode mode) {
    if (layout_mode_ == mode) {
        return;
    }
    layout_mode_ = mode;
    MarkLayoutDirty();
}

Widget::LayoutMode Widget::GetLayoutMode() const {
    return layout_mode_;
}

void Widget::MarkDirty() {
    dirty_bits_ |= DirtyVisual;
    if (engine_) {
        engine_->AddDirty(RectInWindow(), DirtyReason::Visual);
    }
}

void Widget::MarkLayoutDirty() {
    if (dirty_bits_ & DirtyLayout) {
        dirty_bits_ |= DirtyVisual;
        return;
    }
    dirty_bits_ |= static_cast<uint16_t>(DirtyLayout | DirtyVisual);
    cache_.layout_valid = false;
    if (parent_) {
        parent_->MarkLayoutDirty();
    }
}

void Widget::MarkMeasureDirty() {
    if (dirty_bits_ & DirtyMeasure) {
        dirty_bits_ |= static_cast<uint16_t>(DirtyLayout | DirtyVisual);
        return;
    }
    cache_.measure_valid = false;
    cache_.layout_valid = false;
    dirty_bits_ |= static_cast<uint16_t>(DirtyMeasure | DirtyLayout | DirtyVisual);
}

void Widget::SetEngine(UIEngine* engine) {
    if (engine_ == engine) {
        return;
    }
    engine_ = engine;
    for (const auto& child : children_) {
        if (child) {
            child->SetEngine(engine_);
        }
    }
}

bool Widget::IsDirty() const {
    return dirty_bits_ != DirtyNone;
}

bool Widget::IsLayoutDirty() const {
    return (dirty_bits_ & DirtyLayout) != 0;
}

bool Widget::IsMeasureDirty() const {
    return (dirty_bits_ & DirtyMeasure) != 0;
}

bool Widget::IsLayoutValid() const {
    return cache_.layout_valid;
}

bool Widget::IsMeasureValid() const {
    return cache_.measure_valid;
}

uint16_t Widget::DirtyBits() const {
    return dirty_bits_;
}


void Widget::SetVisible(bool v) {
    if (flags_.visible == static_cast<uint8_t>(v)) {
        return;
    }
    flags_.visible = v ? 1 : 0;
    app_ui::debug::PrintVisibleChange(id_, this, v);
    MarkDirty();
    if (engine_) {
        engine_->MarkFocusDirty();
    }
}

bool Widget::Visible() const {
    return flags_.visible != 0;
}

void Widget::SetEnabled(bool v) {
    if (flags_.enabled == static_cast<uint8_t>(v)) {
        return;
    }
    flags_.enabled = v ? 1 : 0;
    MarkDirty();
    if (engine_) {
        engine_->MarkFocusDirty();
    }
}

void Widget::SetFocusable(bool v) {
    if (flags_.focusable == static_cast<uint8_t>(v)) {
        return;
    }
    flags_.focusable = v ? 1 : 0;
    MarkDirty();
    if (engine_) {
        engine_->MarkFocusDirty();
    }
}

void Widget::SetFocused(bool v) {
    if (flags_.focused == static_cast<uint8_t>(v)) {
        return;
    }
    flags_.focused = v ? 1 : 0;
    MarkDirty();
}

bool Widget::Focused() const {
    return flags_.focused != 0;
}

bool Widget::Enabled() const {
    return flags_.enabled != 0;
}

Rect Widget::RectInParent() const {
    return cache_.layout;
}

Rect Widget::DeclaredRect() const {
    return design_rect_;
}

void Widget::SetRectInParent(const Rect& rect) {
    design_rect_ = rect;
    cache_.layout_valid = false;
    dirty_bits_ |= static_cast<uint16_t>(DirtyLayout | DirtyVisual);
    layout_mode_ = rect.IsEmpty() ? LayoutMode::MatchParent : LayoutMode::Fixed;
    if (parent_) {
        parent_->MarkLayoutDirty();
    }
}

void Widget::SetFontName(const char* name) {
    const char* next = (name && name[0]) ? name : kDefaultFontName;
    if (font_.name == next) {
        return;
    }
    font_.name = next;
    MarkMeasureDirty();
    MarkDirty();
}

const char* Widget::FontName() const {
    return (font_.name && font_.name[0]) ? font_.name : kDefaultFontName;
}

Rect Widget::LocalRect() const {
    return {0, 0, cache_.layout.w, cache_.layout.h};
}

Rect Widget::RectInWindow() const {
    return cache_.global_rect;
}

Rect Widget::RectInScreen() const {
    return RectInWindow();
}

Point Widget::MapToGlobal(Point local) const {
    return {static_cast<int16_t>(cache_.global_rect.x + local.x),
            static_cast<int16_t>(cache_.global_rect.y + local.y)};
}

Point Widget::MapFromGlobal(Point global) const {
    return {static_cast<int16_t>(global.x - cache_.global_rect.x),
            static_cast<int16_t>(global.y - cache_.global_rect.y)};
}

bool Widget::HitTest(Point global) const {
    if (!Visible()) {
        return false;
    }
    return cache_.global_rect.Contains(global);
}

} // namespace app_ui

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
    OnTextChanged();
    MarkMeasureDirty();
    MarkDirty();
}

void TextWidget::SetTextId(uint32_t text_id) {
    if (text_id_ == text_id) {
        return;
    }
    text_id_ = text_id;
    MarkMeasureDirty();
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

void LabelWidget::SetProfile(const LabelProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const LabelProfile& LabelWidget::Profile() const {
    return profile_;
}

void LabelWidget::OnDraw(Painter& p) {
    if (Focused() && profile_.focus_invert) {
        p.SetDrawColor(Color::Black);
        p.FillRect(LocalRect());
        p.SetTextColor(Color::White);
    } else {
        p.SetTextColor(Color::Black);
    }
    p.DrawText({profile_.text_offset_x, profile_.text_offset_y}, text_.c_str());
}

void ButtonWidget::SetProfile(const ButtonProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const ButtonProfile& ButtonWidget::Profile() const {
    return profile_;
}

void ButtonWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    const bool focused = Focused() && profile_.focus_invert;
    p.SetDrawColor(focused ? Color::Black : Color::White);
    p.FillRect(rect);
    p.SetDrawColor(Color::Black);
    p.DrawRect(rect);
    p.SetTextColor(focused ? Color::White : Color::Black);
    const Size text_size = p.MeasureText(text_.c_str(), nullptr);
    int16_t x = static_cast<int16_t>((rect.w - text_size.w) / 2);
    int16_t y = static_cast<int16_t>((rect.h - text_size.h) / 2);
    if (x < profile_.min_text_x) {
        x = profile_.min_text_x;
    }
    if (y < profile_.min_text_y) {
        y = profile_.min_text_y;
    }
    p.DrawText({x, y}, text_.c_str());
}

void ImageWidget::SetProfile(const ImageProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const ImageProfile& ImageWidget::Profile() const {
    return profile_;
}

void ImageWidget::SetQrCode(int size, const std::vector<uint8_t>& modules) {
    if (size <= 0 || static_cast<size_t>(size * size) != modules.size()) {
        ClearQrCode();
        return;
    }
    qr_size_ = size;
    qr_modules_ = modules;
    MarkDirty();
}

void ImageWidget::ClearQrCode() {
    qr_size_ = 0;
    qr_modules_.clear();
    MarkDirty();
}

bool ImageWidget::HasQrCode() const {
    return qr_size_ > 0 && static_cast<size_t>(qr_size_ * qr_size_) == qr_modules_.size();
}

void ImageWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();

    if (!text_.empty()) {
        eteacher::app_ui::BinImage icon;
        if (eteacher::app_ui::LoadBinImage(text_, &icon) && icon.data && icon.width > 0 && icon.height > 0) {
            if (auto* ep = dynamic_cast<EpdPainter*>(&p)) {
                auto& gfx = ep->Gfx();
                const Point offset = ep->Offset();
                const int16_t draw_x = static_cast<int16_t>(offset.x + std::max<int16_t>(0, (rect.w - static_cast<int16_t>(icon.width)) / 2));
                const int16_t draw_y = static_cast<int16_t>(offset.y + std::max<int16_t>(0, (rect.h - static_cast<int16_t>(icon.height)) / 2));
                gfx.drawBitmap(draw_x, draw_y, icon.data, static_cast<int16_t>(icon.width), static_cast<int16_t>(icon.height), GxEPD_BLACK);
                return;
            }
        }
    }

    if (profile_.draw_border) {
        p.SetDrawColor(Color::Black);
        p.DrawRect(rect);
    }
    Rect inner = {1, 1, static_cast<int16_t>(rect.w - 2), static_cast<int16_t>(rect.h - 2)};
    if (inner.w > 0 && inner.h > 0) {
        p.SetDrawColor(Color::White);
        p.FillRect(inner);
    }

    if (HasQrCode() && inner.w > 0 && inner.h > 0) {
        constexpr int kQuietZoneModules = 2;
        const int total_modules = qr_size_ + kQuietZoneModules * 2;
        if (total_modules > 0) {
            const int scale_x = inner.w / total_modules;
            const int scale_y = inner.h / total_modules;
            const int module_px = std::min(scale_x, scale_y);
            if (module_px >= 1) {
                const int qr_px = total_modules * module_px;
                const int offset_x = inner.x + (inner.w - qr_px) / 2;
                const int offset_y = inner.y + (inner.h - qr_px) / 2;
                p.SetDrawColor(Color::Black);
                for (int y = 0; y < qr_size_; ++y) {
                    for (int x = 0; x < qr_size_; ++x) {
                        const size_t idx = static_cast<size_t>(y * qr_size_ + x);
                        if (idx >= qr_modules_.size() || qr_modules_[idx] == 0) {
                            continue;
                        }
                        const int px = offset_x + (x + kQuietZoneModules) * module_px;
                        const int py = offset_y + (y + kQuietZoneModules) * module_px;
                        p.FillRect({static_cast<int16_t>(px), static_cast<int16_t>(py),
                                    static_cast<int16_t>(module_px), static_cast<int16_t>(module_px)});
                    }
                }
                return;
            }
        }
    }

    if (!text_.empty()) {
        p.SetTextColor(Color::Black);
        p.DrawText({2, 2}, text_.c_str());
    }
}

TextAreaWidget::TextAreaWidget() {
    SetFontName("wenquanyi_11pt");
}

void TextAreaWidget::SetProfile(const TextAreaProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const TextAreaProfile& TextAreaWidget::Profile() const {
    return profile_;
}

void TextAreaWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    p.SetDrawColor(Color::Black);
    p.SetTextColor(Color::Black);

    if (profile_.decoration_mode == TextAreaProfile::DecorationMode::Box) {
        p.DrawRect(rect);
    } else {
        const int16_t y = rect.h > 0 ? static_cast<int16_t>(rect.h - 1) : 0;
        const int16_t start_x = 2;
        const int16_t end_x = rect.w > 2 ? static_cast<int16_t>(rect.w - 2) : 0;
        for (int16_t x = start_x; x < end_x; x = static_cast<int16_t>(x + 4)) {
            int seg_w = 2;
            if (x + seg_w > end_x) {
                seg_w = end_x - x;
            }
            if (seg_w > 0) {
                p.DrawHLine({x, y}, seg_w);
            }
        }
    }

    if (!text_.empty()) {
        p.DrawText({2, 2}, text_.c_str());
    }
}

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
    model_ = std::make_unique<StaticVectorModel>(std::move(items));
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
    model_ = std::make_unique<TextParsedModel>(text_);
    ClampSelection();
}

int ListViewWidget::EffectiveRowCount() const {
    if (profile_.rows > 0) {
        return profile_.rows;
    }
    if (EffectiveColCount() > 1) {
        const int count = ItemCount();
        if (count <= 0) {
            return 1;
        }
        return std::max(1, static_cast<int>((count + EffectiveColCount() - 1) / EffectiveColCount()));
    }
    return ItemCount();
}

int ListViewWidget::EffectiveColCount() const {
    return std::max(1, profile_.cols);
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
    if (phase == InputPhase::Capture) {
        return InputResult::Continue;
    }
    if (!Enabled() || !Visible()) {
        return InputResult::Continue;
    }
    if (!IsKeyDownOrRepeat(e)) {
        return InputResult::Continue;
    }

    ClampSelection();
    if (ItemCount() <= 0) {
        return InputResult::Continue;
    }

    return InputResult::Continue;
}

FocusIntent TabViewWidget::OnFocusKey(KeyCode key) {
    ClampSelection();
    if (ItemCount() <= 0) {
        return FocusIntent::Bubble;
    }

    const int count = ItemCount();
    if (key == KeyCode::Up) {
        if (selected_index_ > 0) {
            selected_index_ -= 1;
            MarkDirty();
            return FocusIntent::Consume;
        }
        return FocusIntent::EscapeUp;
    }
    if (key == KeyCode::Down) {
        if (selected_index_ + 1 < count) {
            selected_index_ += 1;
            MarkDirty();
            return FocusIntent::Consume;
        }
        return FocusIntent::EscapeDown;
    }
    if (key == KeyCode::Left) {
        return FocusIntent::EscapeLeft;
    }
    if (key == KeyCode::Right) {
        return FocusIntent::EscapeRight;
    }
    return FocusIntent::None;
}

void TabViewWidget::SetItems(std::vector<std::string> items) {
    model_ = std::make_unique<StaticVectorModel>(std::move(items));
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
    model_ = std::make_unique<TextParsedModel>(text_);
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
        for (int i = 1; i <= divider_count; ++i) {
            const int y = rect.h * i / (divider_count + 1);
            p.DrawHLine({2, static_cast<int16_t>(y)}, rect.w - 4);
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

SoftKeyboardWidget::SoftKeyboardWidget() {
    SetFocusable(true);
    SetFontName("wenquanyi_11pt");
}

void SoftKeyboardWidget::SetProfile(const SoftKeyboardProfile& profile) {
    profile_ = profile;
    page_ = (profile_.page < 0) ? 0 : (profile_.page >= kKeyboardPages ? (kKeyboardPages - 1) : profile_.page);
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

        int row = selected_index_ / kKeyboardCols;
        int col = selected_index_ % kKeyboardCols;
        if (profile_.wrap_navigation) {
            if (key == KeyCode::Up) {
                row = (row + kKeyboardRows - 1) % kKeyboardRows;
            } else if (key == KeyCode::Down) {
                row = (row + 1) % kKeyboardRows;
            } else if (key == KeyCode::Left) {
                col = (col + kKeyboardCols - 1) % kKeyboardCols;
            } else if (key == KeyCode::Right) {
                col = (col + 1) % kKeyboardCols;
            }
        } else {
            if (key == KeyCode::Up) {
                row = std::max(0, row - 1);
            } else if (key == KeyCode::Down) {
                row = std::min(kKeyboardRows - 1, row + 1);
            } else if (key == KeyCode::Left) {
                col = std::max(0, col - 1);
            } else if (key == KeyCode::Right) {
                col = std::min(kKeyboardCols - 1, col + 1);
            }
        }
        const int next_index = row * kKeyboardCols + col;
        if (next_index != selected_index_) {
            selected_index_ = next_index;
            MarkDirty();
        }
        return InputResult::Consume;
    }

    if (key == KeyCode::C) {
        last_nav_key_ = -1;
        const char* label = KeyLabel(page_, selected_index_);
        if (label && label[0]) {
            if (std::strcmp(label, "空格") == 0) {
                last_output_ = " ";
            } else {
                last_output_ = label;
            }
            if (on_key_) {
                on_key_(this, last_output_.c_str(), on_key_ctx_);
            }
        }
        return InputResult::Consume;
    }

    if (key == KeyCode::D) {
        last_nav_key_ = -1;
        page_ = (page_ + 1) % kKeyboardPages;
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
    selected_index_ = ClampIndexByCount(selected_index_, kKeyboardPageSize);
}

const char* SoftKeyboardWidget::KeyLabel(int page, int index) const {
    if (index < 0 || index >= kKeyboardPageSize) {
        return "";
    }
    switch (page) {
        case 0:
            return kKeyboardPage1[index];
        case 1:
            return kKeyboardPage2[index];
        case 2:
            return kKeyboardPage3[index];
        default:
            return "";
    }
}

void SoftKeyboardWidget::OnDraw(Painter& p) {
    ClampSelection();
    Rect rect = LocalRect();
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    p.SetDrawColor(Color::White);
    p.FillRect(rect);

    const GridLayoutMetrics metrics = MakeGridLayoutMetrics(rect, kKeyboardRows, kKeyboardCols);

    for (int r = 0; r < metrics.rows; ++r) {
        const int y = r * metrics.cell_h;
        const int h = (r == metrics.rows - 1) ? (rect.h - y) : metrics.cell_h;
        for (int c = 0; c < metrics.cols; ++c) {
            const int x = c * metrics.cell_w;
            const int w = (c == metrics.cols - 1) ? (rect.w - x) : metrics.cell_w;
            const int index = r * kKeyboardCols + c;
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
    DrawGridOutline(p, rect, metrics);
}

void TopBarWidget::OnDraw(Painter& p) {
    auto* epd_painter = dynamic_cast<EpdPainter*>(&p);
    if (!epd_painter || !epd_painter->Epd()) {
        if (profile_.inverted) {
            DrawSimpleBarFallback(p, LocalRect(), text_);
        } else {
            Rect rect = LocalRect();
            p.SetDrawColor(Color::White);
            p.FillRect(rect);
            p.SetTextColor(Color::Black);
            if (!text_.empty()) {
                p.DrawText({profile_.text_offset_x, profile_.text_offset_y}, text_.c_str());
            }
        }
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
    if (!epd_painter || !epd_painter->Epd()) {
        if (profile_.inverted) {
            DrawSimpleBarFallback(p, LocalRect(), text_);
        } else {
            Rect rect = LocalRect();
            p.SetDrawColor(Color::White);
            p.FillRect(rect);
            p.SetTextColor(Color::Black);
            if (!text_.empty()) {
                p.DrawText({profile_.text_offset_x, profile_.text_offset_y}, text_.c_str());
            }
        }
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
    const bool focused = Focused();
    p.SetDrawColor(focused ? Color::Black : Color::White);
    p.FillRect(rect);
    p.SetDrawColor(focused ? Color::White : Color::Black);
    p.SetTextColor(focused ? Color::White : Color::Black);
    const int box = std::min(10, rect.h > 0 ? rect.h : 10);
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
    p.DrawText({static_cast<int16_t>(box + 4), 0}, text_.c_str());
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
    const int radius = std::min(5, rect.h / 2);
    const bool focused = Focused();
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
    p.DrawText({static_cast<int16_t>(radius * 2 + 4), 0}, text_.c_str());
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
    Rect rect = LocalRect();
    const int box_w = 28;
    const int box_h = std::min(12, rect.h > 0 ? rect.h : 12);
    const bool is_on = profile_.checked;

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
    p.SetDrawColor(Color::Black);
    p.DrawRect(rect);
    if (rect.w > 2 && rect.h > 2 && percent > 0) {
        const int fill_w = (rect.w - 2) * percent / 100;
        p.FillRect({1, 1, static_cast<int16_t>(fill_w), static_cast<int16_t>(rect.h - 2)});
    }
}

} // namespace app_ui
