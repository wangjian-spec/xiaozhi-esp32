#pragma once

#include <memory>

#include "eteacher/app_manager/app_base.h"

// A simple demo app: free conversation with push-to-talk support.
class FreeConversationApp : public AppBase {
public:
    FreeConversationApp();

    MenuMeta GetMenuMeta() const override;
    void OnEnter(AppContext &ctx) override;
    void OnExit(AppContext &ctx) override;
    void OnButton(AppContext &ctx, const ButtonEvent &event) override;

private:
    bool listening_ = false;
};

std::unique_ptr<AppBase> MakeFreeConversationApp();
