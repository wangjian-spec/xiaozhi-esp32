#pragma once

#include <memory>
#include <vector>
#include <string>

#include "app_manager/app_base.h"

// Demo app: choose a scene and answer.
class SceneConversationApp : public AppBase {
public:
    SceneConversationApp();

    MenuMeta GetMenuMeta() const override;
    void OnEnter(AppContext &ctx) override;
    void OnExit(AppContext &ctx) override;
    void OnButton(AppContext &ctx, const ButtonEvent &event) override;

private:
    void Render(AppContext &ctx);

    std::vector<std::string> scenes_;
    int index_ = 0;
    bool running_ = false;
};

std::unique_ptr<AppBase> MakeSceneConversationApp();
