#include "eteacher/apps/dictionary/dictionary.h"

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "eteacher/app_ui/renderer.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/widget_builder.h"
#include "eteacher/app_ui/ui_json_loader.h"

#include "display.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <cJSON.h>

namespace {


struct DrawCtx {
    CustomEpdDisplay* epd;
    std::shared_ptr<app_ui::Widget> root;
};

void RenderWidgetTree(CustomEpdDisplay* epd, std::shared_ptr<app_ui::Widget> root) {
    if (!epd || !root) {
        return;
    }

    auto cb = [](Adafruit_GFX& gfx, void* ctx) {
        auto* d = static_cast<DrawCtx*>(ctx);
        if (!d || !d->epd || !d->root) {
            return;
        }

        gfx.fillScreen(GxEPD_WHITE);

        app_ui::LayoutEngine layout_engine;
        const app_ui::Rect viewport{0, 0, static_cast<int16_t>(d->epd->width()),
                                    static_cast<int16_t>(d->epd->height())};
        d->root->SetRectInParent(viewport);
        layout_engine.LayoutTree(d->root.get(), viewport);

        app_ui::RenderList list;
        list.Build(d->root.get());

        app_ui::DirtyTracker dirty;
        dirty.Add(viewport, app_ui::DirtyReason::Full);

        app_ui::Renderer renderer;
        app_ui::EpdPainter painter(d->epd, gfx);
        renderer.Render(list, dirty, painter);
    };

    auto* ctx = new DrawCtx{epd, std::move(root)};
    EpdManager::GetInstance().Schedule(
        EpdManager::TaskType::kPartial,
        cb,
        ctx,
        [](void* p) { delete static_cast<DrawCtx*>(p); },
        EpdManager::Rect(0, 0, epd->width(), epd->height()));
}

} // namespace

MenuMeta DictionaryApp::GetMenuMeta() const {
    return MenuMeta{"dictionary", "词典", "示例"};
}

void DictionaryApp::OnEnter(AppContext &ctx) {
    if (ui_root_) {
        cJSON_Delete(ui_root_);
        ui_root_ = nullptr;
    }

    epd_ = dynamic_cast<CustomEpdDisplay*>(ctx.board.GetDisplay());

    ui_root_ = app_ui::LoadUiJson("dictionary");
    if (!ui_root_) {
        printf("[Dictionary] Embedded JSON parse FAILED\n");
    } else {
        printf("[Dictionary] Embedded JSON parsed successfully\n");
    }

    scene_ids_.clear();
    if (ui_root_) {
        const cJSON* scenes = cJSON_GetObjectItemCaseSensitive(ui_root_, "pages");
        if (cJSON_IsObject(scenes)) {
            const cJSON* scene = nullptr;
            cJSON_ArrayForEach(scene, scenes) {
                if (scene && scene->string) {
                    scene_ids_.push_back(scene->string);
                }
            }
        }
    }

    scene_index_ = 0;
    Render(ctx);
}

void DictionaryApp::OnExit(AppContext &ctx) {
    if (ui_root_) {
        cJSON_Delete(ui_root_);
        ui_root_ = nullptr;
    }
    epd_ = nullptr;
    scene_ids_.clear();
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Dictionary UI");
}

void DictionaryApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
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

void DictionaryApp::PrevScene(AppContext &ctx) {
    if (scene_ids_.empty()) {
        return;
    }
    scene_index_ = (scene_index_ - 1 + static_cast<int>(scene_ids_.size())) % static_cast<int>(scene_ids_.size());
    Render(ctx);
}

void DictionaryApp::NextScene(AppContext &ctx) {
    if (scene_ids_.empty()) {
        return;
    }
    scene_index_ = (scene_index_ + 1) % static_cast<int>(scene_ids_.size());
    Render(ctx);
}

void DictionaryApp::Render(AppContext &ctx) {
    if (!ui_root_) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: json missing");
        return;
    }

    if (scene_ids_.empty()) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: no scenes");
        return;
    }

    const std::string& scene_id = scene_ids_[scene_index_];
    if (!scene_mgr_.LoadFromJson(ui_root_, scene_id.c_str(), static_cast<uint16_t>(scene_index_))) {
        printf("[Dictionary] LoadFromJson failed for scene: %s\n", scene_id.c_str());
        ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: failed to load scene");
        return;
    }

    if (epd_) {
        RenderWidgetTree(epd_, scene_mgr_.RootShared());
        return;
    }

    std::string msg = "Dictionary UI\n";
    msg += "Scene: ";
    msg += "#";
    msg += std::to_string(scene_mgr_.SceneId());
    msg += "\nUse Up/Down to switch";
    ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
}

std::unique_ptr<AppBase> MakeDictionaryApp() {
    return std::make_unique<DictionaryApp>();
}
