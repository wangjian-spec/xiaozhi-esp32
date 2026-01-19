#pragma once

#include <memory>
#include <vector>
#include <string>

#include "app_base.h"
#include "eteacher/app_manager/menu.h"

// AppManager：负责菜单渲染与按键分发。按键事件由板级代码调用 HandleButton 传入。
class AppManager {
public:
    static AppManager& GetInstance();

    // 在 main.cc 中调用，传入 Board 引用。
    void Init(Board &board);

    // 注册 App 后会显示在菜单。
    void Register(std::unique_ptr<AppBase> app);

    // 所有 App 注册完成后调用，刷新主菜单一次。
    void FinalizeRegistration();

    // 绘制菜单。
    void ShowMenu();

    // 设置主菜单底栏提示文字。
    void SetMenuFooterText(std::string text);

    // 外部按键事件入口。
    void HandleButton(const ButtonEvent &event);

    // 菜单显示时强制刷新一次。
    void RefreshMenu();

    // 主动驱动 tick，可由定时器或循环调用。
    void Tick(uint32_t delta_ms);

private:
    AppManager() = default;

    void EnterCurrent();
    void ExitCurrent();
    void EnsureSelectionValid();
    void RenderMenu();
    void RenderStatus(const std::string &headline, const std::string &detail);

    AppContext *ctx_ = nullptr;
    std::vector<std::unique_ptr<AppBase>> apps_;
    int selected_index_ = 0;
    AppBase *running_ = nullptr;
    bool menu_ready_ = false;
    std::string menu_footer_text_ = "Start：打开 App  上下左右：选择 App";

    eteacher::app_menu::Menu menu_;
    eteacher::app_menu::MenuController menu_controller_;
    eteacher::app_menu::MenuLayout last_layout_;
    int last_time_minute_ = -1;
    int last_battery_level_ = -1;
    bool last_wifi_connected_ = false;
    uint32_t menu_tick_accum_ = 0;
};
