#include "eteacher/apps/dictionary/dictionary.h"

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/ui_json_loader.h"

#include "display.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <cJSON.h>

MenuMeta DictionaryApp::GetMenuMeta() const {
    return MenuMeta{"dictionary", "词典", "示例"};
}

void DictionaryApp::OnEnter(AppContext &ctx) {
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

void DictionaryApp::OnExit(AppContext &ctx) {
    ui_ready_ = false;
    ui_root_.reset();
    epd_ = nullptr;
    router_.Reset();
    ui_engine_.Reset();
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Dictionary UI");
}

void DictionaryApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
    HandleAppLevelKeys(ctx, event);
}

void DictionaryApp::PrevScene(AppContext &ctx) {
    if (!router_.HasScenes() || router_.Index() == 0) {
        return;
    }
    if (router_.Prev(ctx)) {
        Render(ctx);
    }
}

void DictionaryApp::NextScene(AppContext &ctx) {
    if (!router_.HasScenes()) {
        return;
    }
    if (router_.Next(ctx)) {
        Render(ctx);
    }
}

void DictionaryApp::Render(AppContext &ctx) {
    if (!ui_ready_) {
        return;
    }
    ui_engine_.RequestRender();
    if (!epd_) {
        std::string msg = "Dictionary UI\n";
        msg += "Scene: ";
        msg += "#";
        msg += std::to_string(scene_runtime_.SceneId());
        msg += "\nUse Up/Down to switch";
        ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
    }
}

bool DictionaryApp::LoadScene(AppContext &ctx, const std::string& scene_id, uint16_t scene_index) {
    if (!scene_runtime_.LoadFromJson(ui_root_.get(), scene_id.c_str(), scene_index)) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: failed to load scene");
        return false;
    }
    auto new_root = scene_runtime_.TakeRoot();
    if (!new_root) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: failed to take scene root");
        return false;
    }
    ui_engine_.SetRoot(std::move(new_root));
    return true;
}

bool DictionaryApp::LoadUi(AppContext &ctx) {
    ui_root_.reset(app_ui::LoadUiJson("dictionary"));
    if (!ui_root_) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: json missing");
        return false;
    }
    auto scene_ids = app_ui::CollectSceneIds(ui_root_.get());
    if (scene_ids.empty()) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: no scenes");
        return false;
    }
    router_.SetScenes(std::move(scene_ids));
    ui_ready_ = true;
    return true;
}

void DictionaryApp::InitUiEngine() {
    ui_engine_.Reset();
    ui_engine_.SetEpd(epd_);
}

void DictionaryApp::HandleAppLevelKeys(AppContext &ctx, const ButtonEvent &event) {
    if (event.action != ButtonAction::Click) {
        return;
    }
    switch (event.id) {
    case AppButton::Up:
    case AppButton::Left:
        PrevScene(ctx);
        break;
    case AppButton::Down:
    case AppButton::Right:
        NextScene(ctx);
        break;
    case AppButton::Start:
    case AppButton::A:
        Render(ctx);
        break;
    default:
        break;
    }
}

std::unique_ptr<AppBase> MakeDictionaryApp() {
    return std::make_unique<DictionaryApp>();
}
