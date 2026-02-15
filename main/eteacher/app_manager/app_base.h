#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "boards/common/board.h"


// AppManager 向应用传递的上下文，便于直接访问 Board。
struct AppContext {
    explicit AppContext(Board &b) : board(b) {}
    Board &board;
};

enum class AppButton {
    Up,
    Down,
    Left,
    Right,
    A,
    B,
    C,
    D,
    Select,
    Start,
    VolumeUp,
    VolumeDown,
};

enum class ButtonAction {
    PressDown,
    PressUp,
    Click,
    LongPress,
    DoubleClick,
    MultipleClick,
};

struct ButtonEvent {
    AppButton id;
    ButtonAction action = ButtonAction::Click;
    uint8_t click_count = 1;
};

struct MenuMeta {
    std::string key;      // 稳定 ID，用于去重或存储最近使用
    std::string title;    // 菜单标题
    std::string subtitle; // 可选说明
};

class AppBase {
public:
    AppBase() = default;
    virtual ~AppBase() = default;

    virtual MenuMeta GetMenuMeta() const = 0;


    const std::string& icon() const { return icon_; }
    void SetIcon(std::string icon) { icon_ = std::move(icon); }

    // 生命周期
    virtual void OnEnter(AppContext &ctx) = 0;
    virtual void OnExit(AppContext &ctx) = 0;
    virtual void OnButton(AppContext &ctx, const ButtonEvent &event) = 0;
    virtual void OnTick(AppContext &ctx) { (void)ctx; }
    virtual bool ShouldInterceptSelectExit() const { return false; }

    bool show_in_menu() const { return show_in_menu_; }

protected:
    bool show_in_menu_ = true;
    std::string icon_;
};

// 简单的回调式 App：只关心进入/退出/按键事件。
class ActionApp : public AppBase {
public:
    ActionApp(MenuMeta meta,
              std::function<void(AppContext &)> on_enter = {},
              std::function<void(AppContext &)> on_exit = {},
              std::function<void(AppContext &, const ButtonEvent &)> on_button = {},
              std::string icon = {});

    MenuMeta GetMenuMeta() const override { return meta_; }
    void OnEnter(AppContext &ctx) override;
    void OnExit(AppContext &ctx) override;
    void OnButton(AppContext &ctx, const ButtonEvent &event) override;

private:
    MenuMeta meta_;
    std::function<void(AppContext &)> on_enter_;
    std::function<void(AppContext &)> on_exit_;
    std::function<void(AppContext &, const ButtonEvent &)> on_button_;
};
