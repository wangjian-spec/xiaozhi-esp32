#pragma once

#include "eteacher/app_manager/app_base.h"

class DictionaryApp : public AppBase {
public:
    DictionaryApp() {}
    virtual ~DictionaryApp() = default;

    // Keep as abstract interface so implementations provide definitions.
    virtual MenuMeta GetMenuMeta() const override = 0;

    virtual void OnEnter(AppContext &ctx) override = 0;
    virtual void OnExit(AppContext &ctx) override = 0;
    virtual void OnButton(AppContext &ctx, const ButtonEvent &event) override = 0;
};

std::unique_ptr<AppBase> MakeDictionaryApp();
