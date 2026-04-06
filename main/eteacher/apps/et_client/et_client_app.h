#pragma once

#include "eteacher/app_manager/app_base.h"
#include "eteacher/apps/et_client/et_client_service.h"

class EtClientApp : public AppBase {
public:
    enum class Page {
        kStatus = 0,
        kAccount = 1,
    };

    enum class AccountAction {
        kSendCodeSmart = 0,
        kSendRegisterCode = 1,
        kRegisterUser = 2,
        kLoginUser = 3,
        kLogout = 4,
    };

    MenuMeta GetMenuMeta() const override;
    void OnEnter(AppContext& ctx) override;
    void OnExit(AppContext& ctx) override;
    void OnButton(AppContext& ctx, const ButtonEvent& event) override;
    void OnTick(AppContext& ctx) override;

private:
    void Render(AppContext& ctx);
    void HandleStatusPage(AppContext& ctx, const ButtonEvent& event);
    void HandleAccountPage(AppContext& ctx, const ButtonEvent& event);

    EtClientState state_{};
    EtClientService service_{};
    Page page_ = Page::kStatus;
    AccountAction account_action_ = AccountAction::kSendCodeSmart;
};

std::unique_ptr<AppBase> MakeEtClientApp();
