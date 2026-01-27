#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "eteacher/app_manager/layout_engine.h"

class Adafruit_GFX;
class CustomEpdDisplay;

namespace eteacher::app_service::tool {

// 语义按键：从物理按键映射而来，Widget 仅处理语义事件。
enum class Action : uint8_t {
    None = 0,
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Back,
    Delete,
    Next,
    Prev,
};

// 所有控件的基类。
// - 只持有 Region 指针（不修改位置/尺寸）
// - Draw 使用 Region.rect 绘制
// - OnAction 处理语义按键
class WidgetBase {
public:
    virtual ~WidgetBase() = default;

    void AttachRegion(const eteacher::layout::Region* region);
    const eteacher::layout::Region* region() const { return region_; }

    // 当前区域矩形（为空时返回空矩形）。
    eteacher::layout::Rect rect() const;

    virtual void Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) = 0;
    virtual bool OnAction(Action action) { (void)action; return false; }
    virtual void OnFocus(bool focused) { focused_ = focused; }

    bool focused() const { return focused_; }

    // 局部刷新：标记脏区域并提供查询。
    bool HasDirty() const { return dirty_; }
    eteacher::layout::Rect DirtyRect() const { return dirty_rect_; }
    void ClearDirty();

protected:
    void MarkDirty(const eteacher::layout::Rect& rect);

private:
    const eteacher::layout::Region* region_ = nullptr;
    bool focused_ = false;
    bool dirty_ = true;
    eteacher::layout::Rect dirty_rect_{};
};

// 文本对齐方式。
enum class TextAlign : uint8_t {
    Left = 0,
    Center,
};

// 文本溢出策略。
enum class TextOverflow : uint8_t {
    Wrap = 0,
    Truncate,
};

// LabelWidget: 多行文本显示，支持自动换行/截断。
// 适用场景：提示文字、说明段落。
// 焦点行为：仅影响可选光标显示（不处理按键）。
class LabelWidget : public WidgetBase {
public:
    void SetText(std::string text);
    const std::string& text() const { return text_; }

    void SetAlign(TextAlign align) { align_ = align; MarkDirty(rect()); }
    void SetOverflow(TextOverflow overflow) { overflow_ = overflow; MarkDirty(rect()); }
    void SetFont(std::string_view font) { font_ = font; MarkDirty(rect()); }
    void SetPadding(int16_t padding) { padding_ = padding; MarkDirty(rect()); }

    void Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) override;

private:
    std::string text_;
    std::string_view font_ = "wenquanyi_11pt";
    TextAlign align_ = TextAlign::Left;
    TextOverflow overflow_ = TextOverflow::Wrap;
    int16_t padding_ = 2;
};

// ButtonWidget: 物理按键焦点按钮。
// 适用场景：确认/提交按钮。
// 焦点行为：focused 时反色显示；Confirm 触发回调。
class ButtonWidget : public WidgetBase {
public:
    void SetLabel(std::string label);
    const std::string& label() const { return label_; }

    void SetFont(std::string_view font) { font_ = font; MarkDirty(rect()); }
    void SetOnConfirm(std::function<void()> cb) { on_confirm_ = std::move(cb); }

    bool OnAction(Action action) override;
    void OnFocus(bool focused) override;
    void Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) override;

private:
    std::string label_;
    std::string_view font_ = "wenquanyi_11pt";
    std::function<void()> on_confirm_{};
};

// ListWidget: 字符串列表，支持滚动与选中高亮。
// 适用场景：菜单、选项列表。
// 焦点行为：Up/Down 改变选中项并自动滚动。
class ListWidget : public WidgetBase {
public:
    void SetItems(const std::vector<std::string>* items);
    int item_count() const;

    void SetSelected(int index);
    int selected() const { return selected_index_; }

    void SetFont(std::string_view font) { font_ = font; MarkDirty(rect()); }
    void SetPadding(int16_t padding) { padding_ = padding; MarkDirty(rect()); }
    void SetInvertHighlight(bool invert) { invert_highlight_ = invert; MarkDirty(rect()); }

    bool OnAction(Action action) override;
    void Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) override;

private:
    int VisibleCount(int16_t line_h) const;
    void ClampSelection();
    void EnsureSelectionVisible(int16_t line_h);
    eteacher::layout::Rect ItemRect(int index, int16_t line_h) const;
    void UpdateDirtyOnSelectionChange(int previous, int previous_top, int16_t line_h);

    const std::vector<std::string>* items_ = nullptr;
    int selected_index_ = 0;
    int top_index_ = 0;
    int last_selected_ = 0;
    int last_top_ = 0;

    std::string_view font_ = "wenquanyi_11pt";
    int16_t padding_ = 2;
    bool invert_highlight_ = true;
};

// TextInputWidget: 文本输入显示，不负责软键盘布局。
// 适用场景：输入框、搜索框。
// 焦点行为：Delete 删除字符；可显示光标。
class TextInputWidget : public WidgetBase {
public:
    void SetText(std::string text);
    const std::string& text() const { return text_; }

    void AppendText(std::string_view utf8);
    void Backspace();

    void SetFont(std::string_view font) { font_ = font; MarkDirty(rect()); }
    void SetPadding(int16_t padding) { padding_ = padding; MarkDirty(rect()); }
    void SetCursorVisible(bool visible) { cursor_visible_ = visible; MarkDirty(rect()); }

    bool OnAction(Action action) override;
    void Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) override;

private:
    std::string text_;
    std::string_view font_ = "wenquanyi_11pt";
    int16_t padding_ = 2;
    bool cursor_visible_ = true;
};

// DialogWidget: Overlay 对话框，标题+内容+按钮列表。
// 适用场景：确认/提示弹窗。
// 焦点行为：Left/Right 切换按钮，Confirm/Back 关闭。
class DialogWidget : public WidgetBase {
public:
    void SetTitle(std::string title);
    void SetContent(std::string content);
    void SetButtons(const std::vector<std::string>* buttons);

    void SetOnConfirm(std::function<void(int)> cb) { on_confirm_ = std::move(cb); }
    void SetOnCancel(std::function<void()> cb) { on_cancel_ = std::move(cb); }

    void SetFont(std::string_view font) { font_ = font; MarkDirty(rect()); }
    void SetPadding(int16_t padding) { padding_ = padding; MarkDirty(rect()); }

    bool OnAction(Action action) override;
    void Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) override;

private:
    void ClampSelection();

    std::string title_;
    std::string content_;
    const std::vector<std::string>* buttons_ = nullptr;
    int selected_index_ = 0;

    std::string_view font_ = "wenquanyi_11pt";
    int16_t padding_ = 4;
    std::function<void(int)> on_confirm_{};
    std::function<void()> on_cancel_{};
};

} // namespace eteacher::app_service::tool
