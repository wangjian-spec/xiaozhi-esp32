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

void LabelWidget::OnDraw(Painter& p) {
    p.SetTextColor(Color::Black);
    p.DrawText({0, 0}, text_.c_str());
}

void ButtonWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    const bool focused = Focused();
    p.SetDrawColor(focused ? Color::Black : Color::White);
    p.FillRect(rect);
    p.SetDrawColor(Color::Black);
    p.DrawRect(rect);
    p.SetTextColor(focused ? Color::White : Color::Black);
    const Size text_size = p.MeasureText(text_.c_str(), nullptr);
    int16_t x = static_cast<int16_t>((rect.w - text_size.w) / 2);
    int16_t y = static_cast<int16_t>((rect.h - text_size.h) / 2);
    if (x < 2) {
        x = 2;
    }
    if (y < 1) {
        y = 1;
    }
    p.DrawText({x, y}, text_.c_str());
}

void ImageWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    p.SetDrawColor(Color::Black);
    p.DrawRect(rect);
    if (!text_.empty()) {
        p.DrawText({2, 2}, text_.c_str());
    }
}

TextAreaWidget::TextAreaWidget() {
    SetFontName("wenquanyi_11pt");
}

void TextAreaWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    p.SetDrawColor(Color::Black);
    p.SetTextColor(Color::Black);
    p.DrawRect(rect);
    if (!text_.empty()) {
        p.DrawText({2, 2}, text_.c_str());
    }
}

TabViewWidget::TabViewWidget() {
    SetFontName("wenquanyi_11pt");
}

InputResult ListViewWidget::OnInput(const InputEvent& e, InputPhase phase) {
    if (phase == InputPhase::Capture) {
        return InputResult::Continue;
    }
    if (!Enabled() || !Visible()) {
        return InputResult::Continue;
    }
    if (e.type != InputType::KeyDown && e.type != InputType::KeyRepeat) {
        return InputResult::Continue;
    }

    ClampSelection();
    if (ItemCount() <= 0) {
        return InputResult::Continue;
    }

    const int key = e.key;
    if (key == static_cast<int>(KeyCode::Start)) {
        if (on_activated_) {
            on_activated_(this, selected_index_, on_activated_ctx_);
        }
        return InputResult::Consume;
    }
    return InputResult::Continue;
}

FocusIntent ListViewWidget::OnFocusKey(KeyCode key) {
    ClampSelection();
    const int count = ItemCount();
    if (count <= 0) {
        return FocusIntent::Bubble;
    }

    if (key == KeyCode::Up) {
        if (selected_index_ > 0) {
            selected_index_--;
            MarkDirty();
            return FocusIntent::Consume;
        }
        return FocusIntent::EscapeUp;
    }
    if (key == KeyCode::Down) {
        if (selected_index_ + 1 < count) {
            selected_index_++;
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

void ListViewWidget::SetRows(int rows) {
    rows_ = rows > 0 ? rows : 0;
    ClampSelection();
    MarkMeasureDirty();
    MarkDirty();
}

int ListViewWidget::Rows() const {
    return rows_;
}

int ListViewWidget::SelectedIndex() const {
    return selected_index_;
}

std::string ListViewWidget::SelectedItem() const {
    const char* label = ItemLabel(selected_index_);
    if (!label) {
        return {};
    }
    return std::string(label);
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
    if (rows_ > 0) {
        return rows_;
    }
    return ItemCount();
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
    const int count = ItemCount();
    if (count <= 0) {
        selected_index_ = 0;
        return;
    }
    const int max_index = count - 1;
    if (selected_index_ < 0) {
        selected_index_ = 0;
    } else if (selected_index_ > max_index) {
        selected_index_ = max_index;
    }
}

void ListViewWidget::OnDraw(Painter& p) {
    ClampSelection();
    Rect rect = LocalRect();
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const int item_count = ItemCount();
    const int item_h = 25;
    const int rows = std::max<int>(1, rect.h / item_h);
    int start_index = 0;
    if (item_count > rows) {
        start_index = selected_index_ - rows / 2;
        if (start_index < 0) {
            start_index = 0;
        }
        const int max_start = item_count - rows;
        if (start_index > max_start) {
            start_index = max_start;
        }
    }
    const bool focused = Focused();

    for (int row = 0; row < rows; ++row) {
        const int y = row * item_h;
        if (y >= rect.h) {
            break;
        }
        const int h = (row == rows - 1) ? std::min(item_h, rect.h - y) : item_h;
        const int item_index = start_index + row;
        const bool selected = focused && (item_index == selected_index_);
        p.SetDrawColor(selected ? Color::Black : Color::White);
        p.FillRect({0, static_cast<int16_t>(y), rect.w, static_cast<int16_t>(h)});
        p.SetTextColor(selected ? Color::White : Color::Black);

        if (item_index < item_count) {
            const char* label = ItemLabel(item_index);
            if (label && label[0]) {
                std::string left_text;
                std::string right_text;
                SplitListItemText(label, left_text, right_text);

                if (!left_text.empty()) {
                    p.DrawText({2, static_cast<int16_t>(y + 2)}, left_text.c_str());
                }
                if (!right_text.empty()) {
                    const Size right_size = p.MeasureText(right_text.c_str(), nullptr);
                    int16_t right_x = static_cast<int16_t>(rect.w - right_size.w - 2);
                    if (right_x < 2) {
                        right_x = 2;
                    }
                    p.DrawText({right_x, static_cast<int16_t>(y + 2)}, right_text.c_str());
                }
            }
        }
    }

    p.SetDrawColor(Color::Black);
    p.DrawRect(rect);
}

void TabViewWidget::OnDraw(Painter& p) {
    ClampSelection();
    Rect rect = LocalRect();
    const int rows = EffectiveRows();
    const int cols = EffectiveCols();
    if (rows <= 0 || cols <= 0 || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const int cell_w = rect.w / cols;
    const int cell_h = rect.h / rows;
    const bool focused = Focused();
    auto* epd_painter = dynamic_cast<EpdPainter*>(&p);

    for (int r = 0; r < rows; ++r) {
        const int y = r * cell_h;
        if (y >= rect.h) {
            break;
        }
        for (int c = 0; c < cols; ++c) {
            const int index = r * cols + c;
            const int x = c * cell_w;
            const int w = (c == cols - 1) ? (rect.w - x) : cell_w;
            const int h = (r == rows - 1) ? std::min(cell_h, rect.h - y) : cell_h;
            const bool selected = focused && (index == selected_index_);

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
            }
        }
    }

    p.SetDrawColor(Color::Black);
    p.DrawRect(rect);
    for (int r = 1; r < rows; ++r) {
        const int y = r * cell_h;
        p.DrawHLine({0, static_cast<int16_t>(y)}, rect.w);
    }
    for (int c = 1; c < cols; ++c) {
        const int x = c * cell_w;
        p.DrawVLine({static_cast<int16_t>(x), 0}, rect.h);
    }
}

InputResult TabViewWidget::OnInput(const InputEvent& e, InputPhase phase) {
    if (phase == InputPhase::Capture) {
        return InputResult::Continue;
    }
    if (!Enabled() || !Visible()) {
        return InputResult::Continue;
    }
    if (e.type != InputType::KeyDown && e.type != InputType::KeyRepeat) {
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

void TabViewWidget::SetGrid(int rows, int cols) {
    if (rows > 0) {
        rows_ = rows;
    }
    if (cols > 0) {
        cols_ = cols;
    }
    ClampSelection();
    MarkMeasureDirty();
    MarkDirty();
}

int TabViewWidget::Rows() const {
    return rows_;
}

int TabViewWidget::Cols() const {
    return cols_;
}

int TabViewWidget::SelectedIndex() const {
    return selected_index_;
}

void TabViewWidget::SetSelectedIndex(int index) {
    const int count = ItemCount();
    if (count <= 0) {
        selected_index_ = index < 0 ? 0 : index;
        MarkDirty();
        return;
    }
    const int max_index = count - 1;
    const int clamped = index < 0 ? 0 : (index > max_index ? max_index : index);
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
    const int count = ItemCount();
    if (count <= 0) {
        selected_index_ = 0;
        return;
    }
    const int max_index = count - 1;
    if (selected_index_ < 0) {
        selected_index_ = 0;
    } else if (selected_index_ > max_index) {
        selected_index_ = max_index;
    }
}

int TabViewWidget::EffectiveRows() const {
    if (rows_ > 0) {
        return rows_;
    }
    return 1;
}

int TabViewWidget::EffectiveCols() const {
    if (cols_ > 0) {
        return cols_;
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
    p.SetDrawColor(Color::Black);
    p.SetTextColor(Color::Black);
    p.DrawRect(rect);
    if (!text_.empty()) {
        p.DrawText({4, 0}, text_.c_str());
    }
}

void MenuWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    p.SetDrawColor(Color::Black);
    p.SetTextColor(Color::Black);
    p.DrawRect(rect);
    const int y1 = rect.h > 0 ? rect.h / 3 : 0;
    const int y2 = rect.h > 0 ? rect.h * 2 / 3 : 0;
    p.DrawHLine({2, static_cast<int16_t>(y1)}, rect.w - 4);
    p.DrawHLine({2, static_cast<int16_t>(y2)}, rect.w - 4);
    if (!text_.empty()) {
        p.DrawText({2, 2}, text_.c_str());
    }
}

void DialogWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    p.SetDrawColor(Color::White);
    p.FillRect(rect);
    p.SetDrawColor(Color::Black);
    p.SetTextColor(Color::Black);
    p.DrawRect(rect);
    if (!text_.empty()) {
        p.DrawText({2, 2}, text_.c_str());
    }
}

SoftKeyboardWidget::SoftKeyboardWidget() {
    SetFocusable(true);
    SetFontName("wenquanyi_11pt");
}

InputResult SoftKeyboardWidget::OnInput(const InputEvent& e, InputPhase phase) {
    if (phase == InputPhase::Capture) {
        return InputResult::Continue;
    }
    if (!Enabled() || !Visible()) {
        return InputResult::Continue;
    }
    if (e.type != InputType::KeyDown && e.type != InputType::KeyRepeat) {
        return InputResult::Continue;
    }

    const KeyCode key = static_cast<KeyCode>(e.key);
    if (key == KeyCode::Up || key == KeyCode::Down || key == KeyCode::Left || key == KeyCode::Right) {
        const bool is_repeat = (e.type == InputType::KeyRepeat);
        const int key_value = static_cast<int>(key);
        uint32_t now_ms = e.timestamp;
        if (is_repeat) {
            constexpr uint32_t kRepeatStepMs = 60;
            if (now_ms == 0) {
                now_ms = last_nav_repeat_ms_ + kRepeatStepMs;
            }
            if (last_nav_key_ == key_value && last_nav_repeat_ms_ != 0 && now_ms > last_nav_repeat_ms_ &&
                (now_ms - last_nav_repeat_ms_) < kRepeatStepMs) {
                return InputResult::Consume;
            }
            last_nav_repeat_ms_ = now_ms;
        } else {
            last_nav_repeat_ms_ = now_ms;
        }
        last_nav_key_ = key_value;

        int row = selected_index_ / kKeyboardCols;
        int col = selected_index_ % kKeyboardCols;
        if (key == KeyCode::Up) {
            row = (row + kKeyboardRows - 1) % kKeyboardRows;
        } else if (key == KeyCode::Down) {
            row = (row + 1) % kKeyboardRows;
        } else if (key == KeyCode::Left) {
            col = (col + kKeyboardCols - 1) % kKeyboardCols;
        } else if (key == KeyCode::Right) {
            col = (col + 1) % kKeyboardCols;
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

int SoftKeyboardWidget::Page() const {
    return page_;
}

void SoftKeyboardWidget::SetPage(int page) {
    const int next = (page < 0) ? 0 : (page >= kKeyboardPages ? (kKeyboardPages - 1) : page);
    if (page_ == next) {
        return;
    }
    page_ = next;
    MarkDirty();
}

int SoftKeyboardWidget::SelectedIndex() const {
    return selected_index_;
}

void SoftKeyboardWidget::SetSelectedIndex(int index) {
    selected_index_ = index;
    ClampSelection();
    MarkDirty();
}

const std::string& SoftKeyboardWidget::LastOutput() const {
    return last_output_;
}

void SoftKeyboardWidget::ClampSelection() {
    if (selected_index_ < 0) {
        selected_index_ = 0;
    } else if (selected_index_ >= kKeyboardPageSize) {
        selected_index_ = kKeyboardPageSize - 1;
    }
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

    const int cell_w = rect.w / kKeyboardCols;
    const int cell_h = rect.h / kKeyboardRows;

    for (int r = 0; r < kKeyboardRows; ++r) {
        const int y = r * cell_h;
        const int h = (r == kKeyboardRows - 1) ? (rect.h - y) : cell_h;
        for (int c = 0; c < kKeyboardCols; ++c) {
            const int x = c * cell_w;
            const int w = (c == kKeyboardCols - 1) ? (rect.w - x) : cell_w;
            const int index = r * kKeyboardCols + c;
            const bool selected = (index == selected_index_);
            p.SetDrawColor(selected ? Color::Black : Color::White);
            p.FillRect({static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)});
            p.SetTextColor(selected ? Color::White : Color::Black);

            const char* label = KeyLabel(page_, index);
            if (label && label[0]) {
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
        }
    }

    p.SetDrawColor(Color::Black);
    p.DrawRect(rect);
    for (int r = 1; r < kKeyboardRows; ++r) {
        p.DrawHLine({0, static_cast<int16_t>(r * cell_h)}, rect.w);
    }
    for (int c = 1; c < kKeyboardCols; ++c) {
        p.DrawVLine({static_cast<int16_t>(c * cell_w), 0}, rect.h);
    }
}

void TopBarWidget::OnDraw(Painter& p) {
    auto* epd_painter = dynamic_cast<EpdPainter*>(&p);
    if (!epd_painter || !epd_painter->Epd()) {
        Rect rect = LocalRect();
        p.SetDrawColor(Color::Black);
        p.FillRect(rect);
        p.SetTextColor(Color::White);
        if (!text_.empty()) {
            p.DrawText({2, 2}, text_.c_str());
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

void BottomBarWidget::OnDraw(Painter& p) {
    auto* epd_painter = dynamic_cast<EpdPainter*>(&p);
    if (!epd_painter || !epd_painter->Epd()) {
        Rect rect = LocalRect();
        p.SetDrawColor(Color::Black);
        p.FillRect(rect);
        p.SetTextColor(Color::White);
        if (!text_.empty()) {
            p.DrawText({2, 2}, text_.c_str());
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
    Rect rect = LocalRect();
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
    Rect rect = LocalRect();
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
    Rect rect = LocalRect();
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
    Rect rect = LocalRect();
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

} // namespace app_ui
