#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "types.h"

// Widget 核心类型与接口
// 本头文件定义了 UI 框架中 Widget 层次结构的基类和各种具体 widget 的接口。

namespace app_ui {
struct InputEvent;
enum class KeyCode : int;
class Painter;
class UIEngine;
class Widget;
enum class InputPhase : uint8_t;

enum class FocusIntent : uint8_t {
    None,
    Consume,
    Bubble,
    EscapeUp,
    EscapeDown,
    EscapeLeft,
    EscapeRight,
};

enum DirtyBits : uint16_t {
    DirtyNone = 0,
    DirtyVisual = 1 << 0,
    DirtyLayout = 1 << 1,
    DirtyMeasure = 1 << 2,
};

struct LayoutCache {
    Size measured{};
    Rect layout{};
    Rect global_rect{};
    bool measure_valid = false;
    bool layout_valid = false;
    uint32_t measure_version = 0;
    uint32_t layout_version = 0;
    Size last_constraint{};
};

struct WidgetFlags {
    uint8_t visible : 1;
    uint8_t enabled : 1;
    uint8_t focusable : 1;
    uint8_t focused : 1;

    WidgetFlags()
        : visible(1),
          enabled(1),
          focusable(0),
                    focused(0) {}
};

class Widget {
public:
    virtual ~Widget() = default;

    Widget* AddChild(std::unique_ptr<Widget> child);
    Widget* Parent() const;
    const std::vector<std::unique_ptr<Widget>>& Children() const;

    Size Measure(const Size& constraint);
    void Layout(const Rect& rect);

    void Draw(Painter& p);
    void Draw(Painter& p, const Rect& dirty);

    virtual InputResult OnInput(const InputEvent& e, InputPhase) { (void)e; return InputResult::Continue; }
    virtual FocusIntent OnFocusKey(KeyCode) { return FocusIntent::None; }

    virtual void OnAttach() {}
    virtual void OnDetach() {}

    void MarkDirty();
    void MarkLayoutDirty();
    void MarkMeasureDirty();

    void SetEngine(UIEngine* engine);
    bool IsDirty() const;
    bool IsLayoutDirty() const;
    bool IsMeasureDirty() const;
    bool IsLayoutValid() const;
    bool IsMeasureValid() const;
    uint16_t DirtyBits() const;

    void SetId(uint32_t id);
    uint32_t Id() const;
    Widget* FindById(uint32_t id);
    const Widget* FindById(uint32_t id) const;

    enum class LayoutMode : uint8_t {
        Fixed,
        MatchParent,
        WrapContent,
    };

    void SetLayoutMode(LayoutMode mode);
    LayoutMode GetLayoutMode() const;

    void SetVisible(bool v);
    bool Visible() const;

    void SetEnabled(bool v);
    bool Enabled() const;

    void SetFocusable(bool v);

    Rect RectInParent() const;
    Rect DeclaredRect() const;
    Rect RectInWindow() const;
    Rect RectInScreen() const;

    Point MapToGlobal(Point local) const;
    Point MapFromGlobal(Point global) const;

    virtual bool HitTest(Point global) const;

    virtual bool Focusable() const { return flags_.focusable; }
    virtual int16_t ZOrder() const { return z_order_; }
    void SetZOrder(int16_t z) { z_order_ = z; }

    void SetFocused(bool v);
    bool Focused() const;

    Size min_size_{0, 0};
    Size max_size_{INT16_MAX, INT16_MAX};
    Size padding_{0, 0};
    Size margin_{0, 0};

    void SetRectInParent(const Rect& rect);

protected:
    Rect LocalRect() const;

protected:
    virtual Size OnMeasure(const Size& constraint) = 0;
    virtual void OnLayout(const Rect& rect) {}
    virtual void OnDraw(Painter& p) = 0;
    virtual void OnDraw(Painter& p, const Rect& dirty);

protected:
    Widget* parent_ = nullptr;
    std::vector<std::unique_ptr<Widget>> children_;

    LayoutCache cache_;
    Rect design_rect_{};
    WidgetFlags flags_;
    uint16_t dirty_bits_ = static_cast<uint16_t>(DirtyLayout | DirtyMeasure | DirtyVisual);
    int16_t z_order_ = 0;
    UIEngine* engine_ = nullptr;
    uint32_t id_ = 0;
    LayoutMode layout_mode_ = LayoutMode::MatchParent;
};

class BasicWidget : public Widget {
public:
    void ApplyStyle(uint16_t style_id);
    uint16_t StyleId() const;

protected:
    Size OnMeasure(const Size& constraint) override;

protected:
    uint16_t style_id_ = kInvalidStyleId;
};

class ContainerWidget : public BasicWidget {
protected:
    Size OnMeasure(const Size& constraint) override;
    void OnDraw(Painter& p) override;
};

class TextWidget : public BasicWidget {
public:
    void SetText(const std::string& text);
    const std::string& Text() const;
    void SetTextId(uint32_t text_id);
    uint32_t TextId() const;

protected:
    virtual void OnTextChanged() {}

protected:
    std::string text_;
    uint32_t text_id_ = 0;
};

class ItemModel {
public:
    virtual ~ItemModel() = default;
    virtual int Count() const = 0;
    virtual const char* Label(int index) const = 0;
};

class LabelWidget : public TextWidget {
protected:
    Size OnMeasure(const Size& constraint) override;
    void OnDraw(Painter& p) override;
};

class ButtonWidget : public LabelWidget {
protected:
    void OnDraw(Painter& p) override;
};

class ImageWidget : public TextWidget {
protected:
    void OnDraw(Painter& p) override;
};

class TextAreaWidget : public TextWidget {
protected:
    void OnDraw(Painter& p) override;
};

class ListViewWidget : public TextWidget {
public:
    InputResult OnInput(const InputEvent& e, InputPhase phase) override;
    FocusIntent OnFocusKey(KeyCode key) override;

    using ActivateCallback = void (*)(ListViewWidget* widget, int index, void* ctx);
    void SetOnActivated(ActivateCallback callback, void* ctx = nullptr);

    void SetItems(std::vector<std::string> items);
    void SetItemModel(std::unique_ptr<ItemModel> model);
    void SetRows(int rows);
    int Rows() const;

    int SelectedIndex() const;
    std::string SelectedItem() const;

protected:
    void OnDraw(Painter& p) override;

    void OnTextChanged() override;

private:
    void SetModelFromText();
    int EffectiveRowCount() const;
    int ItemCount() const;
    const char* ItemLabel(int index) const;
    void ClampSelection();

    int rows_ = 0;
    int selected_index_ = 0;
    std::unique_ptr<ItemModel> model_{};
    ActivateCallback on_activated_ = nullptr;
    void* on_activated_ctx_ = nullptr;
};

class TabViewWidget : public TextWidget {
public:
    InputResult OnInput(const InputEvent& e, InputPhase phase) override;
    FocusIntent OnFocusKey(KeyCode key) override;

    void SetItems(std::vector<std::string> items);
    void SetItemModel(std::unique_ptr<ItemModel> model);
    void SetGrid(int rows, int cols);
    int Rows() const;
    int Cols() const;

    int SelectedIndex() const;
    void SetSelectedIndex(int index);
    std::string SelectedItem() const;

protected:
    void OnDraw(Painter& p) override;

    void OnTextChanged() override;

private:
    void SetModelFromText();
    int ItemCount() const;
    const char* ItemLabel(int index) const;
    void ClampSelection();
    int EffectiveRows() const;
    int EffectiveCols() const;

    int rows_ = 1;
    int cols_ = 0;
    int selected_index_ = 0;
    std::unique_ptr<ItemModel> model_{};
};

class FrameWidget : public TextWidget {
protected:
    void OnDraw(Painter& p) override;
};

class MenuWidget : public TextWidget {
protected:
    void OnDraw(Painter& p) override;
};

class DialogWidget : public TextWidget {
protected:
    void OnDraw(Painter& p) override;
};

class SoftKeyboardWidget : public BasicWidget {
public:
    SoftKeyboardWidget();

    InputResult OnInput(const InputEvent& e, InputPhase phase) override;
    bool Focusable() const override { return true; }

    using KeyCallback = void (*)(SoftKeyboardWidget* widget, const char* value, void* ctx);
    void SetOnKey(KeyCallback callback, void* ctx = nullptr);

    int Page() const;
    void SetPage(int page);
    int SelectedIndex() const;
    void SetSelectedIndex(int index);
    const std::string& LastOutput() const;

protected:
    void OnDraw(Painter& p) override;

private:
    void ClampSelection();
    const char* KeyLabel(int page, int index) const;

    int page_ = 0;
    int selected_index_ = 0;
    std::string last_output_{};
    KeyCallback on_key_ = nullptr;
    void* on_key_ctx_ = nullptr;
};

class TopBarWidget : public TextWidget {
protected:
    void OnDraw(Painter& p) override;
};

class BottomBarWidget : public TextWidget {
protected:
    void OnDraw(Painter& p) override;
};

class CheckboxWidget : public TextWidget {
public:
    void SetChecked(bool checked);
    bool Checked() const;

protected:
    void OnDraw(Painter& p) override;

private:
    bool checked_ = false;
};

class RadioWidget : public TextWidget {
public:
    void SetChecked(bool checked);
    bool Checked() const;

protected:
    void OnDraw(Painter& p) override;

private:
    bool checked_ = false;
};

class SwitchWidget : public TextWidget {
public:
    void SetChecked(bool checked);
    bool Checked() const;

protected:
    void OnDraw(Painter& p) override;

private:
    bool checked_ = false;
};

class ProgressWidget : public TextWidget {
public:
    void SetValue(uint8_t value);
    uint8_t Value() const;

protected:
    void OnDraw(Painter& p) override;

private:
    uint8_t value_ = 0;
};

} // namespace app_ui
