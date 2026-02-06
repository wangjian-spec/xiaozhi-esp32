#pragma once

#include <memory>
#include <string>
#include <vector>

#include "eteacher/app_manager/app_base.h"
#include "eteacher/app_ui/scene.h"

struct cJSON;
class CustomEpdDisplay;

class DeviceSettingApp : public AppBase {
public:
    MenuMeta GetMenuMeta() const override;

    void OnEnter(AppContext &ctx) override;
    void OnExit(AppContext &ctx) override;
    void OnButton(AppContext &ctx, const ButtonEvent &event) override;

private:
    void Render(AppContext &ctx);
    void PrevPage(AppContext &ctx);
    void NextPage(AppContext &ctx);

    app_ui::runtime::SceneManager scene_mgr_{};
    // Owns UI JSON AST for the entire app lifetime.
    cJSON* ui_root_ = nullptr;
    std::vector<std::string> page_ids_{};
    int page_index_ = 0;
    CustomEpdDisplay* epd_ = nullptr;
};

std::unique_ptr<AppBase> MakeDeviceSettingApp();
