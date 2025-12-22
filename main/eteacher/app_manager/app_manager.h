#pragma once

#include <memory>
#include <vector>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ui/app_base.h"
#include "input/button_manager.h"

class AppManager {
public:
    static AppManager& GetInstance();

    // Initialize UI framework and register default apps derived from the reference project.
    void Init();
    void Register(std::shared_ptr<AppBase> app);

    void ShowHome();
    void HandleButton(ButtonId id);

private:
    AppManager() = default;
    void EnsureUiTask();
    static void UiTask(void* arg);
    void UiLoop();

    void EnterSelected();
    void ExitCurrent();
    void RenderHome();
    void RenderStatus(const std::string& headline, const std::string& detail);

    std::vector<std::shared_ptr<AppBase>> apps_;
    int selected_index_ = 0;
    std::shared_ptr<AppBase> current_;
    TaskHandle_t task_handle_ = nullptr;
    bool inited_ = false;
};
