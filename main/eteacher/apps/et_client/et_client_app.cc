#include "eteacher/apps/et_client/et_client_app.h"

#include <string>

#include "display.h"

MenuMeta EtClientApp::GetMenuMeta() const {
    return MenuMeta{"et_client", "ET Client", "Identity bootstrap"};
}

void EtClientApp::OnEnter(AppContext& ctx) {
    std::string error;
    service_.InitializeState(state_, error);
    if (!error.empty()) {
        state_.last_error_code = "INIT_FAILED";
        state_.last_error_message = error;
    }
    Render(ctx);
}

void EtClientApp::OnExit(AppContext& ctx) {
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit ET Client");
}

void EtClientApp::OnButton(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ButtonAction::Click) {
        return;
    }

    if (event.id == AppButton::Start) {
        std::string error;
        if (!service_.EnsureDeviceIdentity(state_, error)) {
            state_.last_error_code = "DEVICE_KEY_CREATE_FAILED";
            state_.last_error_message = error;
        }
        Render(ctx);
        return;
    }

    if (event.id == AppButton::A) {
        service_.ClearUserSession(state_);
        state_.last_success_message = "User session cleared";
        Render(ctx);
        return;
    }
}

void EtClientApp::OnTick(AppContext& ctx) {
    (void)ctx;
}

void EtClientApp::Render(AppContext& ctx) {
    std::string message = "ET Client\n";
    message += state_.device_key_ready ? "Device Key: ready\n" : "Device Key: missing\n";
    message += "Registered: ";
    message += state_.device_registered ? "yes\n" : "no\n";
    message += "User Login: ";
    message += state_.user_logged_in ? "yes\n" : "no\n";
    message += "Device ID: ";
    if (state_.device.device_id.empty()) {
        message += "--\n";
    } else {
        message += state_.device.device_id.substr(0, 16) + "...\n";
    }
    if (!state_.last_error_message.empty()) {
        message += "Last Error: " + state_.last_error_message + "\n";
    } else if (!state_.last_success_message.empty()) {
        message += "Last OK: " + state_.last_success_message + "\n";
    }
    message += "Start: init key\n";
    message += "A: clear user";
    ctx.board.GetDisplay()->SetChatMessage("system", message.c_str());
}

std::unique_ptr<AppBase> MakeEtClientApp() {
    return std::make_unique<EtClientApp>();
}
