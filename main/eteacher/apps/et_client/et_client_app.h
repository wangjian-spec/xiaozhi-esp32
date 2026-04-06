#pragma once

#include "eteacher/app_manager/app_base.h"
#include "eteacher/apps/et_client/et_client_service.h"

class EtClientApp : public AppBase {
public:
    MenuMeta GetMenuMeta() const override;
    void OnEnter(AppContext& ctx) override;
    void OnExit(AppContext& ctx) override;
    void OnButton(AppContext& ctx, const ButtonEvent& event) override;
    void OnTick(AppContext& ctx) override;

private:
    void Render(AppContext& ctx);

    EtClientState state_{};
    EtClientService service_{};
};

std::unique_ptr<AppBase> MakeEtClientApp();
