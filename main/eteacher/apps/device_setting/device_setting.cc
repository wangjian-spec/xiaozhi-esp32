#include "eteacher/apps/device_setting/device_setting.h"

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "eteacher/app_ui/renderer.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/widget_builder.h"
#include "eteacher/app_ui/ui_json_loader.h"

#include "display.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <cJSON.h>

namespace {

static constexpr int kPadding = 8;

// Embedded JSON removed — UI JSON is loaded from flash at runtime.

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

MenuMeta DeviceSettingApp::GetMenuMeta() const {
    return MenuMeta{"device_setting", "系统设置", "示例"};
}


void DeviceSettingApp::OnEnter(AppContext &ctx) {
    if (ui_root_) {
        cJSON_Delete(ui_root_);
        ui_root_ = nullptr;
    }

    epd_ = dynamic_cast<CustomEpdDisplay*>(ctx.board.GetDisplay());

    ui_root_ = app_ui::LoadUiJson("device_setting");

    // Notify user on-screen when running in host/dev environment
    if (ui_root_) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: JSON loaded");
    } else {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: failed to parse JSON");
    }
    page_ids_.clear();
    if (ui_root_) {
        const cJSON* pages = cJSON_GetObjectItemCaseSensitive(ui_root_, "pages");
        if (cJSON_IsObject(pages)) {
            const cJSON* page = nullptr;
            cJSON_ArrayForEach(page, pages) {
                if (page && page->string) {
                    page_ids_.push_back(page->string);
                }
            }
        }
    }

    page_index_ = 0;
    Render(ctx);
}

void DeviceSettingApp::OnExit(AppContext &ctx) {
    if (ui_root_) {
        cJSON_Delete(ui_root_);
        ui_root_ = nullptr;
    }
    epd_ = nullptr;
    page_ids_.clear();
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Device Setting UI");
}

void DeviceSettingApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
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

void DeviceSettingApp::PrevPage(AppContext &ctx) {
    if (page_ids_.empty()) {
        return;
    }
    page_index_ = (page_index_ - 1 + static_cast<int>(page_ids_.size())) % static_cast<int>(page_ids_.size());
    Render(ctx);
}

void DeviceSettingApp::NextPage(AppContext &ctx) {
    if (page_ids_.empty()) {
        return;
    }
    page_index_ = (page_index_ + 1) % static_cast<int>(page_ids_.size());
    Render(ctx);
}

void DeviceSettingApp::Render(AppContext &ctx) {
    if (!ui_root_) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: json missing");
        return;
    }

    if (page_ids_.empty()) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: no pages");
        return;
    }

    const std::string& page_id = page_ids_[page_index_];
    if (!scene_mgr_.LoadFromJson(ui_root_, page_id.c_str(), static_cast<uint16_t>(page_index_))) {
        printf("[DeviceSetting] LoadFromJson failed for page: %s\n", page_id.c_str());
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: failed to load page");
        return;
    }

    if (epd_) {
        RenderWidgetTree(epd_, scene_mgr_.RootShared());
        return;
    }

    std::string msg = "Device Setting UI\n";
    msg += "Page: ";
    msg += "#";
    msg += std::to_string(scene_mgr_.SceneId());
    msg += "\nUse Up/Down to switch";
    ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
}

std::unique_ptr<AppBase> MakeDeviceSettingApp() {
    return std::make_unique<DeviceSettingApp>();
}
