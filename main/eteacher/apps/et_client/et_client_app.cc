#include "eteacher/apps/et_client/et_client_app.h"

#include <string>
#include <vector>

#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "display.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "eteacher/apps/et_client/et_client_http.h"

namespace {

constexpr const char* kTitleFont = "wenquanyi_11pt";
constexpr const char* kBodyFont = "wenquanyi_9pt";
constexpr int16_t kPadding = 12;
constexpr int16_t kLineHeight = 16;
constexpr int16_t kTitleBaseline = 24;

const char* AccountActionLabel(EtClientApp::AccountAction action) {
    switch (action) {
        case EtClientApp::AccountAction::kSendCodeSmart:
            return "Smart Send Code";
        case EtClientApp::AccountAction::kSendRegisterCode:
            return "Register Code";
        case EtClientApp::AccountAction::kRegisterUser:
            return "Register User";
        case EtClientApp::AccountAction::kLoginUser:
            return "Login User";
        case EtClientApp::AccountAction::kLogout:
            return "Logout";
        default:
            return "--";
    }
}

std::string MaskPassword(const std::string& password) {
    if (password.empty()) {
        return "--";
    }
    return std::string(password.size(), '*');
}

std::vector<std::string> BuildStatusLines(const EtClientState& state) {
    std::vector<std::string> lines;
    lines.reserve(10);
    lines.emplace_back(state.device_key_ready ? "Device Key: ready" : "Device Key: missing");
    lines.emplace_back(std::string("Registered: ") + (state.device_registered ? "yes" : "no"));
    lines.emplace_back(std::string("Device Auth: ") + (state.device_authed ? "yes" : "no"));
    lines.emplace_back(std::string("User Login: ") + (state.user_logged_in ? "yes" : "no"));
    lines.emplace_back("Base URL:");
    lines.push_back(EtClientHttp::GetBaseUrl());
    if (state.device.device_id.empty()) {
        lines.emplace_back("Device ID: --");
    } else {
        lines.emplace_back("Device ID: " + state.device.device_id.substr(0, 16) + "...");
    }
    if (!state.last_error_message.empty()) {
        lines.emplace_back("Error: " + state.last_error_message);
    } else if (!state.last_success_message.empty()) {
        lines.emplace_back("OK: " + state.last_success_message);
    }
    return lines;
}

std::vector<std::string> BuildAccountLines(const EtClientState& state, EtClientApp::AccountAction action) {
    std::vector<std::string> lines;
    lines.reserve(10);
    lines.emplace_back("Phone: " + (state.input_phone.empty() ? std::string("--") : state.input_phone));
    lines.emplace_back("Password: " + MaskPassword(state.input_password));
    lines.emplace_back("Code: " + (state.input_code.empty() ? std::string("--") : state.input_code));
    lines.emplace_back(std::string("Selected: ") + AccountActionLabel(action));
    lines.emplace_back("Current input editing pending.");
    if (!state.last_error_message.empty()) {
        lines.emplace_back("Error: " + state.last_error_message);
    } else if (!state.last_success_message.empty()) {
        lines.emplace_back("OK: " + state.last_success_message);
    }
    return lines;
}

}  // namespace

MenuMeta EtClientApp::GetMenuMeta() const {
    return MenuMeta{"et_client", "ET Client", "Identity bootstrap"};
}

void EtClientApp::OnEnter(AppContext& ctx) {
    std::string error;
    service_.InitializeState(state_, error);
    if (state_.input_phone.empty()) {
        state_.input_phone = "+8613800138000";
    }
    if (state_.input_password.empty()) {
        state_.input_password = "e2e_password_123";
    }
    if (state_.input_code.empty()) {
        state_.input_code = "123456";
    }
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

    if (event.id == AppButton::Left) {
        page_ = Page::kStatus;
        Render(ctx);
        return;
    }

    if (event.id == AppButton::Right) {
        page_ = Page::kAccount;
        Render(ctx);
        return;
    }

    if (page_ == Page::kStatus) {
        HandleStatusPage(ctx, event);
        return;
    }

    HandleAccountPage(ctx, event);
}

void EtClientApp::HandleStatusPage(AppContext& ctx, const ButtonEvent& event) {
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
        std::string error;
        service_.EnsureDeviceRegistered(state_, error);
        if (!error.empty()) {
            state_.last_error_code = "REGISTER_FAILED";
            state_.last_error_message = error;
        }
        Render(ctx);
        return;
    }

    if (event.id == AppButton::B) {
        std::string error;
        service_.EnsureDeviceAuthed(state_, error);
        if (!error.empty()) {
            state_.last_error_code = "AUTH_FAILED";
            state_.last_error_message = error;
        }
        Render(ctx);
        return;
    }

    if (event.id == AppButton::C) {
        service_.ClearUserSession(state_);
        state_.last_success_message = "User session cleared";
        Render(ctx);
        return;
    }
}

void EtClientApp::HandleAccountPage(AppContext& ctx, const ButtonEvent& event) {
    if (event.id == AppButton::Up) {
        if (account_action_ == AccountAction::kSendCodeSmart) {
            account_action_ = AccountAction::kLogout;
        } else {
            account_action_ = static_cast<AccountAction>(static_cast<int>(account_action_) - 1);
        }
        Render(ctx);
        return;
    }

    if (event.id == AppButton::Down) {
        if (account_action_ == AccountAction::kLogout) {
            account_action_ = AccountAction::kSendCodeSmart;
        } else {
            account_action_ = static_cast<AccountAction>(static_cast<int>(account_action_) + 1);
        }
        Render(ctx);
        return;
    }

    if (event.id != AppButton::Start) {
        return;
    }

    std::string error;
    bool ok = false;
    switch (account_action_) {
        case AccountAction::kSendCodeSmart:
            ok = service_.SendCodeSmart(state_, state_.input_phone, error);
            break;
        case AccountAction::kSendRegisterCode:
            ok = service_.SendRegisterCode(state_, state_.input_phone, error);
            break;
        case AccountAction::kRegisterUser:
            ok = service_.RegisterUser(state_, state_.input_phone, state_.input_code, state_.input_password, error);
            break;
        case AccountAction::kLoginUser:
            ok = service_.LoginUser(state_, state_.input_phone, state_.input_code, state_.input_password, error);
            break;
        case AccountAction::kLogout:
            ok = service_.Logout(state_, error);
            break;
    }
    if (!ok && !error.empty()) {
        state_.last_error_message = error;
    }
    Render(ctx);
}

void EtClientApp::OnTick(AppContext& ctx) {
    (void)ctx;
}

void EtClientApp::Render(AppContext& ctx) {
    auto* epd = dynamic_cast<CustomEpdDisplay*>(ctx.board.GetDisplay());
    const auto lines = (page_ == Page::kStatus)
        ? BuildStatusLines(state_)
        : BuildAccountLines(state_, account_action_);
    if (!epd) {
        std::string message = "ET Client\n";
        for (const auto& line : lines) {
            message += line;
            message += "\n";
        }
        if (page_ == Page::kStatus) {
            message += "L/R switch  Start:init  A:reg\n";
            message += "B:auth  C:clear user";
        } else {
            message += "L/R switch  Up/Down:action\n";
            message += "Start:run";
        }
        ctx.board.GetDisplay()->SetChatMessage("system", message.c_str());
        return;
    }

    struct DrawCtx {
        CustomEpdDisplay* epd;
        std::vector<std::string> lines;
    };

    auto* draw_ctx = new DrawCtx{epd, lines};
    auto cb = [](Adafruit_GFX& gfx, void* ctx_ptr) {
        auto* draw = static_cast<DrawCtx*>(ctx_ptr);
        if (!draw || !draw->epd) {
            return;
        }

        gfx.fillScreen(GxEPD_WHITE);
        int16_t baseline = kTitleBaseline;
        draw->epd->DrawUtf8(kPadding, baseline, "ET Client", kTitleFont, GxEPD_BLACK);
        baseline = static_cast<int16_t>(baseline + kLineHeight);
        for (const auto& line : draw->lines) {
            if (baseline >= draw->epd->height() - 24) {
                break;
            }
            draw->epd->DrawUtf8(kPadding, baseline, line, kBodyFont, GxEPD_BLACK);
            baseline = static_cast<int16_t>(baseline + kLineHeight);
        }
        const bool account_page = draw->lines.size() >= 4 && draw->lines[3].rfind("Selected:", 0) == 0;
        const char* footer1 = account_page ? "Up/Down:action  Start:run"
                                           : "Start:init  A:register  B:auth";
        const char* footer2 = account_page ? "Left:status  Select:exit"
                                           : "C:clear user  Right:account";
        draw->epd->DrawUtf8(kPadding, draw->epd->height() - 18, footer1, kBodyFont, GxEPD_BLACK);
        draw->epd->DrawUtf8(kPadding, draw->epd->height() - 4, footer2, kBodyFont, GxEPD_BLACK);
    };

    EpdManager::GetInstance().Schedule(EpdManager::TaskType::kPartial,
                                       cb,
                                       draw_ctx,
                                       [](void* ctx_ptr) { delete static_cast<DrawCtx*>(ctx_ptr); },
                                       EpdManager::Rect(0, 0, epd->width(), epd->height()));
}

std::unique_ptr<AppBase> MakeEtClientApp() {
    return std::make_unique<EtClientApp>();
}
