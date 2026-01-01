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
    Select,
    Back,
    Ptt,
    PttAlt,
};

struct ButtonEvent {
    AppButton id;
    bool long_press = false;
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

    // 生命周期
    virtual void OnEnter(AppContext &ctx) = 0;
    virtual void OnExit(AppContext &ctx) = 0;
    virtual void OnButton(AppContext &ctx, const ButtonEvent &event) = 0;
    virtual void OnTick(AppContext &ctx, uint32_t /*delta_ms*/) { (void)ctx; }

    bool show_in_menu() const { return show_in_menu_; }

protected:
    bool show_in_menu_ = true;
};

// 简单的回调式 App：只关心进入/退出/按键事件。
class ActionApp : public AppBase {
public:
    ActionApp(MenuMeta meta,
              std::function<void(AppContext &)> on_enter = {},
              std::function<void(AppContext &)> on_exit = {},
              std::function<void(AppContext &, const ButtonEvent &)> on_button = {});

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
