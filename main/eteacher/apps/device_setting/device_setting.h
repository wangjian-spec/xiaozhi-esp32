#pragma once

#include <memory>
#include <string>

#include "app_manager/app_base.h"

class DeviceSettingApp : public AppBase {
public:
    DeviceSettingApp();

    MenuMeta GetMenuMeta() const override;
    void OnEnter(AppContext &ctx) override;
    void OnExit(AppContext &ctx) override;
    void OnButton(AppContext &ctx, const ButtonEvent &event) override;
    void OnTick(AppContext &ctx, uint32_t delta_ms) override;

private:
    enum class View {
        kMenu,
        kWifiStatus,
        kWifiQr,
        kOta,
    };

    struct OtaUiState {
        bool task_running = false;
        bool check_done = false;
        bool has_new_version = false;
        std::string current_version;
        std::string new_version;
        std::string url;
        std::string status_line;
        int progress = -1; // 0-100
        bool upgrade_done = false;
        bool upgrade_ok = false;
    };

    View view_ = View::kMenu;
    int selected_index_ = 0;
    bool last_wifi_config_mode_ = false;

    OtaUiState ota_;

    void Render(AppContext &ctx);
    void RenderMenu(AppContext &ctx);
    void RenderWifiStatus(AppContext &ctx);
    void RenderWifiQr(AppContext &ctx);
    void RenderOta(AppContext &ctx);

    void EnterWifiQr(AppContext &ctx, bool force_config);
    void CycleLanguage(AppContext &ctx);
    void EnterOta(AppContext &ctx);
};

std::unique_ptr<AppBase> MakeDeviceSettingApp();
