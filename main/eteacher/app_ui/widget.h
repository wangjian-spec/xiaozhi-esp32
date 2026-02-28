#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "types.h"
#include "renderer.h"

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

    void SetFontName(const char* name);
    const char* FontName() const;

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
    Font font_{};
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

class ListViewWidget;
class DialogWidget;

struct ListViewProfile {
    int rows = 0;
    int cols = 1;
    bool selection_enabled = true;
    bool focus_highlight_enabled = true;
    bool activation_enabled = true;
    bool split_item_text_by_tab = true;
    bool parse_marker_prefix = true;
};

class ListViewBehavior {
public:
    virtual ~ListViewBehavior() = default;
    virtual InputResult OnInput(ListViewWidget& widget, const InputEvent& e, InputPhase phase) = 0;
    virtual FocusIntent OnFocusKey(ListViewWidget& widget, KeyCode key) = 0;
    virtual void OnDraw(ListViewWidget& widget, Painter& p) = 0;
};

struct DialogProfile {
    enum class Mode : uint8_t {
        PlainText,
        Prompt,
        Grid,
    };

    Mode mode = Mode::PlainText;
    int grid_rows = 2;
    int grid_cols = 0;
    bool navigation_enabled = false;
    bool selection_highlight_enabled = true;
    bool activation_on_key_c = true;
    int16_t prompt_button_height = 24;
    int prompt_max_lines = 8;
    int16_t text_offset_x = 2;
    int16_t text_offset_y = 2;
    std::string confirm_label = "确认";
    std::string cancel_label = "取消";
};

struct TextAreaProfile {
    enum class DecorationMode : uint8_t {
        Box,
        UnderlineDashed,
    };

    DecorationMode decoration_mode = DecorationMode::Box;
    int max_lines = 1;
    int16_t text_offset_x = 2;
    int16_t text_offset_y = 2;
    int16_t line_gap_px = 2;
    int16_t underline_margin_x = 2;
    int16_t underline_segment = 2;
    int16_t underline_gap = 2;
};

struct LabelProfile {
    bool focus_invert = true;
    bool draw_border = false;
    bool draw_rounded_border = false;
    int16_t corner_radius = 6;
    bool center_text_h = false;
    bool center_text_v = false;
    bool draw_left_prefix = false;
    bool center_text_full_rect = false;
    std::string left_prefix{};
    int16_t left_prefix_gap = 4;
    int16_t text_offset_x = 0;
    int16_t text_offset_y = 0;
};

struct ButtonProfile {
    bool focus_invert = true;
    int16_t min_text_x = 2;
    int16_t min_text_y = 1;
};

struct ImageProfile {
    bool draw_border = true;
    bool draw_fallback_text = true;
    int16_t content_inset = 1;
    int16_t text_offset_x = 2;
    int16_t text_offset_y = 2;
    int quiet_zone_modules = 2;
};

struct TabViewProfile {
    enum class NavigationMode : uint8_t {
        Vertical,
        Horizontal,
        Grid,
    };

    int rows = 1;
    int cols = 0;
    NavigationMode navigation_mode = NavigationMode::Vertical;
    bool selection_enabled = true;
    bool wrap_navigation = false;
    bool focus_highlight_enabled = true;
};

struct FrameProfile {
    bool draw_border = true;
    int16_t text_offset_x = 4;
    int16_t text_offset_y = 2;
    int16_t min_line_height = 10;
};

struct MenuProfile {
    bool draw_border = true;
    int divider_count = 2;
    int16_t divider_margin_x = 2;
    int16_t text_offset_x = 2;
    int16_t text_offset_y = 2;
};

struct SoftKeyboardProfile {
    int page = 0;
    int selected_index = 0;
    int rows = 4;
    int cols = 9;
    int pages = 3;
    bool wrap_navigation = true;
    uint32_t nav_repeat_step_ms = 60;
    bool activation_enabled = true;
    bool page_switch_enabled = true;
    KeyCode activation_key = static_cast<KeyCode>(6);
    KeyCode page_switch_key = static_cast<KeyCode>(7);
    bool draw_grid_outline = true;
    std::string space_label = "空格";
    std::string space_output = " ";
    std::vector<std::string> key_labels{};
};

struct BarProfile {
    enum class RenderMode : uint8_t {
        Generic,
        Native,
    };

    bool inverted = true;
    int16_t text_offset_x = 2;
    int16_t text_offset_y = 2;
    int16_t inverted_text_offset_x = 2;
    int16_t inverted_text_offset_y = 2;
    RenderMode render_mode = RenderMode::Native;
};

struct CheckboxProfile {
    bool checked = false;
    bool focus_invert = true;
    int16_t box_size = 10;
    int16_t text_offset_x = 4;
};

struct RadioProfile {
    bool checked = false;
    bool focus_invert = true;
    int16_t radius = 5;
    int16_t text_offset_x = 4;
};

struct SwitchProfile {
    bool checked = false;
    bool focus_invert = false;
    bool invert_when_on = true;
    int16_t box_width = 28;
    int16_t box_height = 12;
    int16_t text_offset_x = 4;
    int16_t state_text_offset_x = 2;
    int16_t state_text_offset_y = 0;
    int16_t state_inset = 1;
    std::string on_label = "ON";
    std::string off_label = "OFF";
};

struct ProgressProfile {
    uint8_t value = 0;
    uint8_t max_value = 100;
    bool draw_border = true;
    int16_t fill_inset = 1;
};

class DialogBehavior {
public:
    virtual ~DialogBehavior() = default;
    virtual InputResult OnInput(DialogWidget& widget, const InputEvent& e, InputPhase phase) = 0;
    virtual void OnDraw(DialogWidget& widget, Painter& p) = 0;
};

class LabelWidget : public TextWidget {
public:
    void SetProfile(const LabelProfile& profile);
    const LabelProfile& Profile() const;

protected:
    Size OnMeasure(const Size& constraint) override;
    void OnDraw(Painter& p) override;

private:
    LabelProfile profile_{};
};

class ButtonWidget : public LabelWidget {
public:
    void SetProfile(const ButtonProfile& profile);
    const ButtonProfile& Profile() const;

protected:
    void OnDraw(Painter& p) override;

private:
    ButtonProfile profile_{};
};

class ImageWidget : public TextWidget {
public:
    void SetProfile(const ImageProfile& profile);
    const ImageProfile& Profile() const;

    void SetQrCode(int size, const std::vector<uint8_t>& modules);
    void ClearQrCode();
    bool HasQrCode() const;

protected:
    void OnDraw(Painter& p) override;

private:
    ImageProfile profile_{};
    int qr_size_ = 0;
    std::vector<uint8_t> qr_modules_{};
};

class TextAreaWidget : public TextWidget {
public:
    TextAreaWidget();
    void SetProfile(const TextAreaProfile& profile);
    const TextAreaProfile& Profile() const;

protected:
    void OnDraw(Painter& p) override;

private:
    TextAreaProfile profile_{};
};

class ListViewWidget : public TextWidget {
public:
    InputResult OnInput(const InputEvent& e, InputPhase phase) override;
    FocusIntent OnFocusKey(KeyCode key) override;

    using ActivateCallback = void (*)(ListViewWidget* widget, int index, void* ctx);
    void SetOnActivated(ActivateCallback callback, void* ctx = nullptr);

    void SetItems(std::vector<std::string> items);
    void SetItemModel(std::unique_ptr<ItemModel> model);

    void SetProfile(const ListViewProfile& profile);
    const ListViewProfile& Profile() const;
    void SetBehavior(std::unique_ptr<ListViewBehavior> behavior);
    void ResetBehavior();

    int SelectedIndex() const;
    void SetSelectedIndex(int index);
    std::string SelectedItem() const;
    void ActivateSelected();

    int ItemCount() const;
    const char* ItemLabel(int index) const;
    void ClampSelection();

protected:
    void OnDraw(Painter& p) override;

    void OnTextChanged() override;

private:
    void SetModelFromText();

    int selected_index_ = 0;
    std::unique_ptr<ItemModel> model_{};
    ActivateCallback on_activated_ = nullptr;
    void* on_activated_ctx_ = nullptr;
    ListViewProfile profile_{};
    std::unique_ptr<ListViewBehavior> behavior_{};
};

class TabViewWidget : public TextWidget {
public:
    TabViewWidget();

    InputResult OnInput(const InputEvent& e, InputPhase phase) override;
    FocusIntent OnFocusKey(KeyCode key) override;

    void SetItems(std::vector<std::string> items);
    void SetItemModel(std::unique_ptr<ItemModel> model);
    void SetProfile(const TabViewProfile& profile);
    const TabViewProfile& Profile() const;

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

    int selected_index_ = 0;
    std::unique_ptr<ItemModel> model_{};
    TabViewProfile profile_{};
};

class FrameWidget : public TextWidget {
public:
    void SetProfile(const FrameProfile& profile);
    const FrameProfile& Profile() const;

protected:
    void OnDraw(Painter& p) override;

private:
    FrameProfile profile_{};
};

class MenuWidget : public TextWidget {
public:
    void SetProfile(const MenuProfile& profile);
    const MenuProfile& Profile() const;

protected:
    void OnDraw(Painter& p) override;

private:
    MenuProfile profile_{};
};

class DialogWidget : public TextWidget {
public:
    InputResult OnInput(const InputEvent& e, InputPhase phase) override;

    void SetProfile(const DialogProfile& profile);
    const DialogProfile& Profile() const;
    void SetBehavior(std::unique_ptr<DialogBehavior> behavior);
    void ResetBehavior();

    void SetItems(std::vector<std::string> items);
    const std::vector<std::string>& Items();
    int SelectedIndex() const;
    void SetSelectedIndex(int index);
    void NotifySelected();

    using SelectCallback = void (*)(DialogWidget* widget, int index, void* ctx);
    void SetOnSelected(SelectCallback callback, void* ctx = nullptr);

protected:
    void OnDraw(Painter& p) override;
    void OnTextChanged() override;

private:
    void EnsureItemsFromText();
    void ClampSelection();

    DialogProfile profile_{};
    std::unique_ptr<DialogBehavior> behavior_{};
    std::vector<std::string> items_{};
    bool items_from_text_cached_ = false;
    int selected_index_ = 0;
    SelectCallback on_selected_ = nullptr;
    void* on_selected_ctx_ = nullptr;
};

class SoftKeyboardWidget : public BasicWidget {
public:
    SoftKeyboardWidget();

    InputResult OnInput(const InputEvent& e, InputPhase phase) override;

    using KeyCallback = void (*)(SoftKeyboardWidget* widget, const char* value, void* ctx);
    void SetOnKey(KeyCallback callback, void* ctx = nullptr);

    void SetProfile(const SoftKeyboardProfile& profile);
    const SoftKeyboardProfile& Profile() const;

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
    int last_nav_key_ = -1;
    uint32_t last_nav_repeat_ms_ = 0;
    std::string last_output_{};
    KeyCallback on_key_ = nullptr;
    void* on_key_ctx_ = nullptr;
    SoftKeyboardProfile profile_{};
};

class TopBarWidget : public TextWidget {
public:
    void SetProfile(const BarProfile& profile);
    const BarProfile& Profile() const;

protected:
    void OnDraw(Painter& p) override;

private:
    BarProfile profile_{};
};

class BottomBarWidget : public TextWidget {
public:
    void SetProfile(const BarProfile& profile);
    const BarProfile& Profile() const;

protected:
    void OnDraw(Painter& p) override;

private:
    BarProfile profile_{};
};

class CheckboxWidget : public TextWidget {
public:
    void SetProfile(const CheckboxProfile& profile);
    const CheckboxProfile& Profile() const;

protected:
    void OnDraw(Painter& p) override;

private:
    CheckboxProfile profile_{};
};

class RadioWidget : public TextWidget {
public:
    void SetProfile(const RadioProfile& profile);
    const RadioProfile& Profile() const;

protected:
    void OnDraw(Painter& p) override;

private:
    RadioProfile profile_{};
};

class SwitchWidget : public TextWidget {
public:
    void SetProfile(const SwitchProfile& profile);
    const SwitchProfile& Profile() const;

protected:
    void OnDraw(Painter& p) override;

private:
    SwitchProfile profile_{};
};

class ProgressWidget : public TextWidget {
public:
    void SetProfile(const ProgressProfile& profile);
    const ProgressProfile& Profile() const;

protected:
    void OnDraw(Painter& p) override;

private:
    ProgressProfile profile_{};
};

} // namespace app_ui
