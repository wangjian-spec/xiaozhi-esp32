#include "eteacher/apps/device_setting/device_setting.h"

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/ui_desc.h"
#if APP_UI_USE_GENERATED_DESC
#include "eteacher/apps/device_setting/device_setting_ui.h"
#else
#include "eteacher/app_ui/ui_json_loader.h"
#endif

#include "display.h"

#include <memory>
#include <string>
#include <vector>

#include <cJSON.h>
#include <esp_timer.h>

#if APP_UI_USE_GENERATED_DESC
namespace {
constexpr const app_ui::desc::UiDesc* kUiDesc = &app_ui::generated::device_setting::kUi;
}
#endif

MenuMeta DeviceSettingApp::GetMenuMeta() const {
    return MenuMeta{"device_setting", "系统设置", "示例"};
}


void DeviceSettingApp::OnEnter(AppContext &ctx) {
    ui_ready_ = false;
    ui_root_.reset();
    router_.Reset();
    scene_load_id_ = 0;
    epd_ = dynamic_cast<CustomEpdDisplay*>(ctx.board.GetDisplay());

    if (!LoadUi(ctx)) {
        return;
    }
    InitUiEngine();
    router_.SetActivateFn([this](AppContext& ctx, size_t /*index*/, const std::string& scene_id) {
        return LoadScene(ctx, scene_id, ++scene_load_id_);
    });
    if (router_.Activate(ctx, 0)) {
        Render(ctx);
    }
}

void DeviceSettingApp::OnExit(AppContext &ctx) {
    ui_ready_ = false;
    ui_root_.reset();
    epd_ = nullptr;
    router_.Reset();
    ui_engine_.Reset();
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Device Setting UI");
}

void DeviceSettingApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
    HandleAppLevelKeys(ctx, event);
}

void DeviceSettingApp::PrevPage(AppContext &ctx) {
    if (!router_.HasScenes() || router_.Index() == 0 || !CanSwitchPage()) {
        return;
    }
    if (router_.Prev(ctx)) {
        Render(ctx);
    }
}

void DeviceSettingApp::NextPage(AppContext &ctx) {
    if (!router_.HasScenes() || !CanSwitchPage()) {
        return;
    }
    if (router_.Next(ctx)) {
        Render(ctx);
    }
}

void DeviceSettingApp::Render(AppContext &ctx) {
    if (!ui_ready_) {
        return;
    }
    ui_engine_.RequestRender();
    if (!epd_) {
        std::string msg = "Device Setting UI\n";
        msg += "Scene: ";
        msg += "#";
        msg += std::to_string(scene_runtime_.SceneId());
        msg += "\nUse Up/Down to switch";
        ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
    }
}

bool DeviceSettingApp::LoadScene(AppContext &ctx, const std::string& scene_id, uint16_t scene_index) {
#if APP_UI_USE_GENERATED_DESC
    if (!scene_runtime_.LoadFromDesc(*kUiDesc, scene_id.c_str(), scene_index)) {
#else
    if (!scene_runtime_.LoadFromJson(ui_root_.get(), scene_id.c_str(), scene_index)) {
#endif
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: failed to load scene");
        return false;
    }
    auto new_root = scene_runtime_.TakeRoot();
    if (!new_root) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: failed to take scene root");
        return false;
    }
    ui_engine_.SetRoot(std::move(new_root));
    return true;
}

bool DeviceSettingApp::LoadUi(AppContext &ctx) {
#if APP_UI_USE_GENERATED_DESC
    auto scene_ids = app_ui::CollectSceneIds(*kUiDesc);
    if (scene_ids.empty()) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: no scenes");
        return false;
    }
    router_.SetScenes(std::move(scene_ids));
    ui_ready_ = true;
    return true;
#else
    ui_root_.reset(app_ui::LoadUiJson("device_setting"));
    if (!ui_root_) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: json missing");
        return false;
    }
    auto scene_ids = app_ui::CollectSceneIds(ui_root_.get());
    if (scene_ids.empty()) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: no scenes");
        return false;
    }
    router_.SetScenes(std::move(scene_ids));
    ui_ready_ = true;
    return true;
#endif
}

void DeviceSettingApp::InitUiEngine() {
    ui_engine_.Reset();
    ui_engine_.SetEpd(epd_);
}

void DeviceSettingApp::HandleAppLevelKeys(AppContext &ctx, const ButtonEvent &event) {
    if (event.action != ButtonAction::Click) {
        return;
    }
    switch (event.id) {
    case AppButton::Up:
    case AppButton::Left:
        PrevPage(ctx);
        break;
    case AppButton::Down:
    case AppButton::Right:
        NextPage(ctx);
        break;
    case AppButton::Start:
    case AppButton::A:
        Render(ctx);
        break;
    default:
        break;
    }
}

bool DeviceSettingApp::CanSwitchPage() {
    const int64_t now = esp_timer_get_time();
    constexpr int64_t kDebounceUs = 50000;
    if (now - last_page_switch_us_ < kDebounceUs) {
        return false;
    }
    last_page_switch_us_ = now;
    return true;
}

std::unique_ptr<AppBase> MakeDeviceSettingApp() {
    return std::make_unique<DeviceSettingApp>();
}
