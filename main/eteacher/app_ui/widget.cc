#include "widget.h"
#include "widgets/widget_internal.h"

#include "renderer.h"
#include "input.h"
#include "ui_engine.h"
#include "eteacher/font_manager/font_manager.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include "debug.h"
namespace app_ui {

namespace widget_internal {

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

int ResolveKeyboardRows(const SoftKeyboardProfile& profile) {
    return std::max(1, profile.rows);
}

int ResolveKeyboardCols(const SoftKeyboardProfile& profile) {
    return std::max(1, profile.cols);
}

int ResolveKeyboardPages(const SoftKeyboardProfile& profile) {
    return std::max(1, profile.pages);
}

int ResolveKeyboardPageSize(const SoftKeyboardProfile& profile) {
    return ResolveKeyboardRows(profile) * ResolveKeyboardCols(profile);
}

bool HasCustomKeyboardLayout(const SoftKeyboardProfile& profile) {
    const int page_size = ResolveKeyboardPageSize(profile);
    const int pages = ResolveKeyboardPages(profile);
    const size_t required = static_cast<size_t>(page_size * pages);
    return !profile.key_labels.empty() && profile.key_labels.size() >= required;
}

const char* DefaultKeyboardLabel(int page, int index) {
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

void DrawSimpleBarFallback(Painter& p,
                           const Rect& rect,
                           const std::string& text,
                           int16_t text_offset_x,
                           int16_t text_offset_y) {
    p.SetDrawColor(Color::Black);
    p.FillRect(rect);
    p.SetTextColor(Color::White);
    if (!text.empty()) {
        p.DrawText({text_offset_x, text_offset_y}, text.c_str());
    }
}

void DrawGenericBar(Painter& p, const Rect& rect, const std::string& text, const BarProfile& profile) {
    if (profile.inverted) {
        DrawSimpleBarFallback(p, rect, text, profile.inverted_text_offset_x, profile.inverted_text_offset_y);
        return;
    }

    p.SetDrawColor(Color::White);
    p.FillRect(rect);
    p.SetTextColor(Color::Black);
    if (!text.empty()) {
        p.DrawText({profile.text_offset_x, profile.text_offset_y}, text.c_str());
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

std::unique_ptr<ItemModel> MakeStaticVectorModel(std::vector<std::string> items) {
    return std::make_unique<StaticVectorModel>(std::move(items));
}

std::unique_ptr<ItemModel> MakeTextParsedModel(const std::string& text) {
    return std::make_unique<TextParsedModel>(text);
}

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
            if (profile.split_item_text_by_tab && label && label[0]) {
                SplitListItemText(label, left_text, right_text);
            } else if (label) {
                left_text = label;
            }

            ListMarkerStyle marker_style = ListMarkerStyle::None;
            if (profile.parse_marker_prefix) {
                ConsumeListMarkerPrefix(left_text, marker_style);
            }
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
        if (IsActivationKey(key) || (profile.activation_on_key_c && key == KeyCode::C)) {
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
            DrawGrid(p, rect, widget.Items(), widget.SelectedIndex(), profile.grid_rows, profile.grid_cols,
                     profile.selection_highlight_enabled);
            return;
        }

        p.DrawRect(rect);
        if (!widget.Text().empty()) {
            p.DrawText({profile.text_offset_x, profile.text_offset_y}, widget.Text().c_str());
        }
    }

private:
    void DrawPrompt(DialogWidget& widget, Painter& p, const Rect& rect) {
        p.DrawRect(rect);

        const int button_h = std::max<int>(1, widget.Profile().prompt_button_height);
        const int content_h = std::max(1, rect.h - button_h - 2);
        const int max_text_w = std::max(1, rect.w - 4);
        const int max_lines = widget.Profile().prompt_max_lines > 0 ? widget.Profile().prompt_max_lines : 8;
        const auto lines = WrapTextByWidth(p, widget.Text(), max_text_w, max_lines);
        const int line_h = std::max<int>(1, p.MeasureText("A", nullptr).h);
        int y = widget.Profile().text_offset_y;
        for (const auto& line : lines) {
            if (y + line_h > content_h) {
                break;
            }
            p.SetTextColor(Color::Black);
            p.DrawText({widget.Profile().text_offset_x, static_cast<int16_t>(y)}, line.c_str());
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

        void DrawGrid(Painter& p,
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

} // namespace widget_internal

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
