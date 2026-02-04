#pragma once

#include <memory>
#include <string>
#include <vector>

#include "eteacher/app_manager/app_base.h"
#include "eteacher/app_ui/scene.h"

struct cJSON;

class DictionaryApp : public AppBase {
public:
    MenuMeta GetMenuMeta() const override;

    void OnEnter(AppContext &ctx) override;
    void OnExit(AppContext &ctx) override;
    void OnButton(AppContext &ctx, const ButtonEvent &event) override;

private:
    void Render(AppContext &ctx);
    void PrevScene(AppContext &ctx);
    void NextScene(AppContext &ctx);

    app_ui::runtime::SceneManager scene_mgr_{};
    cJSON* ui_root_ = nullptr;
    std::vector<std::string> scene_ids_{};
    int scene_index_ = 0;
};

std::unique_ptr<AppBase> MakeDictionaryApp();
