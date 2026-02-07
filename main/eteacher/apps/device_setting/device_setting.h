#pragma once

#include <memory>
#include <string>
#include "eteacher/app_manager/app_base.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/ui_engine.h"
#include "eteacher/app_ui/ui_router.h"

struct cJSON;
class CustomEpdDisplay;

class DeviceSettingApp : public AppBase {
public:
    MenuMeta GetMenuMeta() const override;

    void OnEnter(AppContext &ctx) override;
    void OnExit(AppContext &ctx) override;
    void OnButton(AppContext &ctx, const ButtonEvent &event) override;

private:
    using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

    bool LoadUi(AppContext &ctx);
    void InitUiEngine();
    void HandleAppLevelKeys(AppContext &ctx, const ButtonEvent &event);
    bool CanSwitchPage();

    bool LoadScene(AppContext &ctx, const std::string& scene_id, uint16_t scene_index);

    void Render(AppContext &ctx);
    void PrevPage(AppContext &ctx);
    void NextPage(AppContext &ctx);

    app_ui::UIEngine ui_engine_{};
    app_ui::runtime::SceneRuntime scene_runtime_{};
    JsonPtr ui_root_{nullptr, cJSON_Delete};
    UiRouter router_{};
    uint16_t scene_load_id_ = 0;
    CustomEpdDisplay* epd_ = nullptr;
    bool ui_ready_ = false;
    int64_t last_page_switch_us_ = 0;
};

std::unique_ptr<AppBase> MakeDeviceSettingApp();
