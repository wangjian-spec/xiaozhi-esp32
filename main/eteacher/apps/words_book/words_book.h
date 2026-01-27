#pragma once

#include "eteacher/app_manager/app_base.h"

class WordsBookApp : public AppBase {
public:
    WordsBookApp() {}

    MenuMeta GetMenuMeta() const override;

    void OnEnter(AppContext &ctx) override;
    void OnExit(AppContext &ctx) override;
    void OnButton(AppContext &ctx, const ButtonEvent &event) override;
};

std::unique_ptr<AppBase> MakeWordsBookApp();
