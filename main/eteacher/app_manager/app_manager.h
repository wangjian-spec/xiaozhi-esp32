#pragma once

#include <memory>
#include <vector>
#include <string>

#include "app_base.h"

// AppManager：负责菜单渲染与按键分发。按键事件由板级代码调用 HandleButton 传入。
class AppManager {
public:
    static AppManager& GetInstance();

    // 在 main.cc 中调用，传入 Board 引用。
    void Init(Board &board);

    // 注册 App 后会显示在菜单。
    void Register(std::unique_ptr<AppBase> app);

    // 绘制菜单。
    void ShowMenu();

    // 外部按键事件入口。
    void HandleButton(const ButtonEvent &event);

    // 主动驱动 tick，可由定时器或循环调用。
    void Tick(uint32_t delta_ms);

private:
    AppManager() = default;

    void EnterCurrent();
    void ExitCurrent();
    void EnsureSelectionValid();
    void MoveSelection(int step);
    void RenderMenu();
    void RenderStatus(const std::string &headline, const std::string &detail);

    AppContext *ctx_ = nullptr;
    std::vector<std::unique_ptr<AppBase>> apps_;
    int selected_index_ = 0;
    AppBase *running_ = nullptr;
};
