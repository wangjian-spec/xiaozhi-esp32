#include "eteacher/apps/device_setting/device_setting.h"

#include <algorithm>

#include <ssid_manager.h>
#include <wifi_manager.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_service/app_service.h"
#include "eteacher/app_ui/input.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/apps/device_setting/device_setting_ui.h"

namespace {
constexpr const char* kTag = "DeviceSettingApp";
constexpr const app_ui::desc::UiDesc* kUiDesc = &app_ui::generated::device_setting::kUi;

constexpr uint32_t kWidgetTabView = 0xA8EC2EDCu;
constexpr uint32_t kWidgetBottomBar = 0x80DAA8ADu;

constexpr uint32_t kWidgetScanButton = 0xF5EFF05Fu;
constexpr uint32_t kWidgetScanList = 0x3EE34E54u;
constexpr uint32_t kWidgetSavedList = 0xBAB5B4F8u;
constexpr uint32_t kWidgetPageAlert = 0x2C695914u;

constexpr uint32_t kWidgetSoftKeyboard = 0xF2B83403u;
constexpr uint32_t kWidgetDialogInputPassword = 0x847FB9EBu;
constexpr uint32_t kWidgetLabelSsid = 0x36D75C59u;
constexpr uint32_t kWidgetLabelPassword = 0xE39189D7u;
constexpr uint32_t kWidgetTextAreaPassword = 0x0F95CADDu;
constexpr uint32_t kWidgetLabelStatusInformation = 0xB55C8996u;

constexpr uint32_t kWidgetDialogHint = 0x2452ECFFu;
constexpr uint32_t kWidgetHintAlert = 0x7F1B0E11u;
constexpr uint32_t kWidgetButtonSubmit = 0xD17B7AB2u;
constexpr uint32_t kWidgetButtonCancel = 0x087A890Eu;

constexpr const char* kInputInitStatus = "请输入WIFI密码,按start连接网络";

bool IsClickLike(const ButtonEvent& event) {
    return event.action == ButtonAction::Click || event.action == ButtonAction::PressDown ||
           event.action == ButtonAction::LongPress;
}

void AppendIfValid(const app_ui::Widget* widget, std::vector<uint32_t>& out) {
    if (!widget) {
        return;
    }
    const uint32_t id = widget->Id();
    if (id != 0) {
        out.push_back(id);
    }
}

bool MapButtonToKey(AppButton button, app_ui::KeyCode& out) {
    switch (button) {
        case AppButton::Up:
            out = app_ui::KeyCode::Up;
            return true;
        case AppButton::Down:
            out = app_ui::KeyCode::Down;
            return true;
        case AppButton::Left:
            out = app_ui::KeyCode::Left;
            return true;
        case AppButton::Right:
            out = app_ui::KeyCode::Right;
            return true;
        case AppButton::A:
            out = app_ui::KeyCode::A;
            return true;
        case AppButton::B:
            out = app_ui::KeyCode::B;
            return true;
        case AppButton::C:
            out = app_ui::KeyCode::C;
            return true;
        case AppButton::D:
            out = app_ui::KeyCode::D;
            return true;
        case AppButton::Select:
            out = app_ui::KeyCode::Select;
            return true;
        case AppButton::Start:
            out = app_ui::KeyCode::Start;
            return true;
        case AppButton::VolumeUp:
            out = app_ui::KeyCode::VolumeUp;
            return true;
        case AppButton::VolumeDown:
            out = app_ui::KeyCode::VolumeDown;
            return true;
        default:
            return false;
    }
}
}

MenuMeta DeviceSettingApp::GetMenuMeta() const {
    return MenuMeta{"device_setting", "系统设置", "Left/Right to switch"};
}

bool DeviceSettingApp::ShouldInterceptSelectExit() const {
    return confirm_dialog_visible_ || keyboard_visible_ || IsInputDialogVisible() || IsHintDialogVisible();
}

void DeviceSettingApp::OnEnter(AppContext &ctx) {
    ui_ready_ = false;
    keyboard_visible_ = false;
    confirm_dialog_visible_ = false;
    confirm_delete_action_ = false;
    scanning_in_progress_ = false;
    has_scanned_once_ = false;
    last_scan_failed_ = false;
    submit_connect_in_progress_ = false;
    confirm_connect_in_progress_ = false;
    submit_cooldown_until_us_ = 0;
    confirm_cooldown_until_us_ = 0;
    hint_alert_text_.clear();
    last_alert_text_.clear();
    last_connected_ssid_.clear();
    router_.Reset();
    scene_load_id_ = 0;
    epd_ = dynamic_cast<CustomEpdDisplay*>(ctx.board.GetDisplay());

    if (!LoadUi(ctx)) {
        return;
    }
    InitUiEngine();
    router_.SetActivateFn([this](AppContext& context, size_t /*index*/, const std::string& scene_id) {
        return LoadScene(context, scene_id, ++scene_load_id_);
    });
    if (router_.Activate(ctx, 0)) {
        Render(ctx);
    }
}

void DeviceSettingApp::OnExit(AppContext &ctx) {
    (void)ctx;
    StopAutoCloseDialog();

    if (connect_disconnected_handler_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, connect_disconnected_handler_);
        connect_disconnected_handler_ = nullptr;
    }
    if (connect_got_ip_handler_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, connect_got_ip_handler_);
        connect_got_ip_handler_ = nullptr;
    }

    ui_ready_ = false;
    keyboard_visible_ = false;
    confirm_dialog_visible_ = false;
    submit_connect_in_progress_ = false;
    confirm_connect_in_progress_ = false;
    submit_cooldown_until_us_ = 0;
    confirm_cooldown_until_us_ = 0;
    hint_alert_text_.clear();
    epd_ = nullptr;
}

void DeviceSettingApp::OnTick(AppContext &ctx) {
    if (!ui_ready_ || !is_wifi_scene_) {
        return;
    }

    const std::string connected_ssid = CurrentConnectedSsid();
    if (connected_ssid == last_connected_ssid_) {
        return;
    }

    last_connected_ssid_ = connected_ssid;
    RefreshNetworkListDisplay();
    UpdateAlertStatusByNetwork();
    Render(ctx);
}

void DeviceSettingApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
    if (!ui_ready_ || !IsClickLike(event)) {
        return;
    }

    if (HandleConfirmDialogButtons(event)) {
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }

    if (!keyboard_visible_ && !confirm_dialog_visible_ && HandleFocusCycle(event)) {
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }

    if (HandleKeyboardButtons(event)) {
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }
    if (HandleTabViewNav(ctx, event)) {
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }
    if (HandleFocusedButtons(event)) {
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }
    if (HandleListViewActivate(event)) {
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }

    SendInputToUi(event);
    UpdateBottomBarHintByFocus();
    Render(ctx);
}

bool DeviceSettingApp::LoadUi(AppContext &ctx) {
    auto scene_ids = app_ui::CollectSceneIds(*kUiDesc);
    if (scene_ids.empty()) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: no scenes");
        return false;
    }
    router_.SetScenes(std::move(scene_ids));
    ui_ready_ = true;
    return true;
}

bool DeviceSettingApp::LoadScene(AppContext &ctx, const std::string& scene_id, uint16_t scene_index) {
    if (!scene_runtime_.LoadFromDesc(*kUiDesc, scene_id.c_str(), scene_index)) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: failed to load scene");
        return false;
    }
    auto new_root = scene_runtime_.TakeRoot();
    if (!new_root) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: failed to take scene root");
        return false;
    }

    root_ = new_root.get();
    BindWidgets(root_);
    last_alert_text_.clear();
    ui_engine_.SetRoot(std::move(new_root));
    UpdateTabSelection();
    BuildFocusCycle(scene_id);
    SyncFocusCycleIndex();
    if (tabview_) {
        ui_engine_.RequestFocus(tabview_->Id());
    }
    HideKeyboard();
    HideConfirmDialog();
    UpdateAlertStatusByNetwork();
    UpdateBottomBarHintByFocus();
    return true;
}

void DeviceSettingApp::InitUiEngine() {
    ui_engine_.Reset();
    ui_engine_.SetEpd(epd_);
}

void DeviceSettingApp::Render(AppContext &ctx) {
    if (!ui_ready_) {
        return;
    }
    if (confirm_dialog_visible_) {
        RefreshHintAlertLabel();
    }
    ui_engine_.RequestRender();
    if (!epd_) {
        std::string msg = "Device Setting UI\n";
        msg += "Scene: #";
        msg += std::to_string(scene_runtime_.SceneId());
        ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
    }
}

void DeviceSettingApp::PrevScene(AppContext &ctx) {
    if (!router_.HasScenes()) {
        return;
    }
    if (router_.Prev(ctx)) {
        UpdateTabSelection();
    }
}

void DeviceSettingApp::NextScene(AppContext &ctx) {
    if (!router_.HasScenes()) {
        return;
    }
    if (router_.Next(ctx)) {
        UpdateTabSelection();
    }
}

void DeviceSettingApp::BindWidgets(app_ui::Widget* root) {
    if (!root) {
        tabview_ = nullptr;
        scan_button_ = nullptr;
        scan_list_ = nullptr;
        saved_list_ = nullptr;
        bottom_bar_ = nullptr;
        keyboard_ = nullptr;
        input_dialog_ = nullptr;
        hint_dialog_ = nullptr;
        button_submit_ = nullptr;
        button_cancel_ = nullptr;
        ssid_label_ = nullptr;
        password_label_ = nullptr;
        status_info_label_ = nullptr;
        page_alert_label_ = nullptr;
        hint_alert_label_ = nullptr;
        password_area_ = nullptr;
        return;
    }

    tabview_ = dynamic_cast<app_ui::TabViewWidget*>(root->FindById(kWidgetTabView));
    bottom_bar_ = dynamic_cast<app_ui::BottomBarWidget*>(root->FindById(kWidgetBottomBar));

    scan_button_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetScanButton));
    scan_list_ = dynamic_cast<app_ui::ListViewWidget*>(root->FindById(kWidgetScanList));
    saved_list_ = dynamic_cast<app_ui::ListViewWidget*>(root->FindById(kWidgetSavedList));
    page_alert_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetPageAlert));

    keyboard_ = dynamic_cast<app_ui::SoftKeyboardWidget*>(root->FindById(kWidgetSoftKeyboard));
    input_dialog_ = dynamic_cast<app_ui::DialogWidget*>(root->FindById(kWidgetDialogInputPassword));
    hint_dialog_ = dynamic_cast<app_ui::DialogWidget*>(root->FindById(kWidgetDialogHint));
    button_submit_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetButtonSubmit));
    button_cancel_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetButtonCancel));

    ssid_label_ = input_dialog_ ? dynamic_cast<app_ui::LabelWidget*>(input_dialog_->FindById(kWidgetLabelSsid)) : nullptr;
    password_label_ = input_dialog_ ? dynamic_cast<app_ui::LabelWidget*>(input_dialog_->FindById(kWidgetLabelPassword)) : nullptr;
    status_info_label_ = input_dialog_ ? dynamic_cast<app_ui::LabelWidget*>(input_dialog_->FindById(kWidgetLabelStatusInformation)) : nullptr;
    password_area_ = input_dialog_ ? dynamic_cast<app_ui::TextAreaWidget*>(input_dialog_->FindById(kWidgetTextAreaPassword)) : nullptr;
    hint_alert_label_ = hint_dialog_ ? dynamic_cast<app_ui::LabelWidget*>(hint_dialog_->FindById(kWidgetHintAlert)) : nullptr;
    if (!hint_alert_label_ && hint_dialog_) {
        hint_alert_label_ = dynamic_cast<app_ui::LabelWidget*>(hint_dialog_->FindById(kWidgetPageAlert));
    }
    if (hint_alert_label_) {
        hint_alert_label_->SetZOrder(30);
    }

    if (keyboard_) {
        keyboard_->SetOnKey(&DeviceSettingApp::OnKeyboardKey, this);
    }
    if (tabview_) {
        tabview_->SetFocusable(true);
    }

    scan_ssids_raw_.clear();
    scan_rssi_raw_.clear();
    scan_results_.clear();
    has_scanned_once_ = false;
    last_scan_failed_ = false;
    RefreshNetworkListDisplay();
    if (scan_list_) {
        scan_list_->SetItems(scan_results_);
    }

    PopulateSavedNetworks();
    HideKeyboard();
    HideConfirmDialog();
    SetInputStatus(kInputInitStatus);
    SetBottomBarHint("");
}

void DeviceSettingApp::UpdateTabSelection() {
    if (!tabview_) {
        return;
    }
    tabview_->SetSelectedIndex(static_cast<int>(router_.Index()));
}

void DeviceSettingApp::PopulateScanResults() {
    scanning_in_progress_ = true;
    SetAlertLabel("网络扫描中");

    scan_ssids_raw_.clear();
    scan_rssi_raw_.clear();
    scan_results_.clear();
    bool scan_failed = false;

    auto& wifi = WifiManager::GetInstance();
    if (!wifi.Initialize()) {
        ESP_LOGW(kTag, "WiFi manager initialize failed");
        scan_failed = true;
    } else {
        auto& app_service = AppService::GetInstance();

        ESP_LOGI(kTag, "Manual WiFi scan requested");
        app_service.BeginWifiScanNoAutoConnect();
        wifi.SetStationScanConsumeEnabled(false);

        wifi_scan_config_t scan_cfg = {};
        scan_cfg.show_hidden = false;

        esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
        if (err == ESP_ERR_WIFI_STATE) {
            ESP_LOGW(kTag, "Scan rejected by WIFI_STATE, restarting station and retrying once");
            wifi.StopStation();
            wifi.StartStation();
            vTaskDelay(pdMS_TO_TICKS(200));
            err = esp_wifi_scan_start(&scan_cfg, true);
        }
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "Manual scan start failed: %s", esp_err_to_name(err));
            scan_failed = true;
        } else {
            constexpr int kConsumeRetryStepMs = 30;
            constexpr int kConsumeRetryTimeoutMs = 600;
            int waited_ms = 0;
            std::vector<std::pair<std::string, int>> scan_results;
            while (waited_ms <= kConsumeRetryTimeoutMs) {
                scan_results = app_service.ConsumeWifiScanResults();
                if (!scan_results.empty()) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(kConsumeRetryStepMs));
                waited_ms += kConsumeRetryStepMs;
            }

            ESP_LOGI(kTag, "Manual scan consumed %u APs after waiting %d ms",
                     static_cast<unsigned>(scan_results.size()), waited_ms);

            for (const auto& item : scan_results) {
                if (item.first.empty()) {
                    continue;
                }
                scan_ssids_raw_.push_back(item.first);
                scan_rssi_raw_.push_back(item.second);
            }
        }
        wifi.SetStationScanConsumeEnabled(true);

        app_service.EndWifiScanNoAutoConnect();
    }

    has_scanned_once_ = true;
    last_scan_failed_ = scan_failed;
    RefreshNetworkListDisplay();

    scanning_in_progress_ = false;
    UpdateAlertStatusByNetwork();
}

void DeviceSettingApp::PopulateSavedNetworks() {
    saved_networks_.clear();
    const auto& ssid_list = SsidManager::GetInstance().GetSsidList();
    for (const auto& item : ssid_list) {
        saved_networks_.push_back(item.ssid);
    }
    if (saved_networks_.empty()) {
        saved_networks_.push_back("(empty)");
    }

    RefreshNetworkListDisplay();
}

void DeviceSettingApp::RefreshNetworkListDisplay() {
    const std::string connected_ssid = CurrentConnectedSsid();

    scan_results_.clear();
    if (last_scan_failed_) {
        scan_results_.push_back("(scan failed)");
    } else if (!scan_ssids_raw_.empty()) {
        for (size_t i = 0; i < scan_ssids_raw_.size(); ++i) {
            const auto& ssid = scan_ssids_raw_[i];
            std::string item = ssid;
            if (!connected_ssid.empty() && ssid == connected_ssid) {
                item += "\t已连接";
            } else if (i < scan_rssi_raw_.size()) {
                item += "\t" + std::to_string(scan_rssi_raw_[i]) + " dBm";
            }
            scan_results_.push_back(std::move(item));
        }
    } else if (has_scanned_once_) {
        scan_results_.push_back("(none)");
    }

    if (scan_list_) {
        scan_list_->SetItems(scan_results_);
    }

    saved_display_items_.clear();
    saved_display_items_.reserve(saved_networks_.size());
    for (const auto& ssid : saved_networks_) {
        if (ssid.empty() || IsPlaceholderItem(ssid)) {
            saved_display_items_.push_back(ssid);
            continue;
        }
        if (!connected_ssid.empty() && ssid == connected_ssid) {
            saved_display_items_.push_back(ssid + "\t已连接");
        } else {
            saved_display_items_.push_back(ssid);
        }
    }
    if (saved_list_) {
        saved_list_->SetItems(saved_display_items_);
    }
}

void DeviceSettingApp::UpdateAlertStatusByNetwork() {
    const std::string ssid = CurrentConnectedSsid();
    if (!ssid.empty()) {
        SetAlertLabel("网络已连接");
        return;
    }
    if (scanning_in_progress_) {
        SetAlertLabel("网络扫描中");
        return;
    }
    if (has_scanned_once_ && !last_scan_failed_) {
        SetAlertLabel("扫描成功");
        return;
    }
    SetAlertLabel("网络未连接");
}

void DeviceSettingApp::SetAlertLabel(const std::string& text) {
    if (last_alert_text_ == text && page_alert_label_ && page_alert_label_->Text() == text) {
        return;
    }
    last_alert_text_ = text;
    if (page_alert_label_) {
        page_alert_label_->SetText(text);
        page_alert_label_->SetVisible(true);
    }
}

void DeviceSettingApp::SetInputStatus(const std::string& text) {
    if (status_info_label_) {
        status_info_label_->SetText(text);
    }
}

void DeviceSettingApp::SetBottomBarHint(const std::string& text) {
    bottom_bar_hint_ = text;
    if (bottom_bar_) {
        bottom_bar_->SetText(bottom_bar_hint_);
    }
}

void DeviceSettingApp::PushBottomBarHint(const std::string& text) {
    SetBottomBarHint(text);
}

void DeviceSettingApp::UpdateBottomBarHintByFocus() {
    if (IsInputDialogVisible() || keyboard_visible_) {
        SetBottomBarHint("软键盘：B退格 C确定 D翻页 Start提交");
        return;
    }
    if (IsHintDialogVisible()) {
        SetBottomBarHint("上下切换 Start/C确认 B取消");
        return;
    }
    if (scan_button_ && scan_button_->Focused()) {
        SetBottomBarHint("按Start/C键开始扫描");
        return;
    }
    if (scan_list_ && scan_list_->Focused()) {
        SetBottomBarHint("C连接");
        return;
    }
    if (saved_list_ && saved_list_->Focused()) {
        SetBottomBarHint("C连接 D删除");
        return;
    }
    SetBottomBarHint("");
}

void DeviceSettingApp::BuildFocusCycle(const std::string& scene_id) {
    is_wifi_scene_ = (scene_id == "page_main");
    focus_cycle_ids_.clear();
    focus_cycle_index_ = 0;
    if (!is_wifi_scene_) {
        return;
    }
    AppendIfValid(tabview_, focus_cycle_ids_);
    AppendIfValid(scan_button_, focus_cycle_ids_);
    AppendIfValid(scan_list_, focus_cycle_ids_);
    AppendIfValid(saved_list_, focus_cycle_ids_);
}

void DeviceSettingApp::SyncFocusCycleIndex() {
    if (focus_cycle_ids_.empty()) {
        focus_cycle_index_ = 0;
        return;
    }
    for (size_t i = 0; i < focus_cycle_ids_.size(); ++i) {
        const uint32_t id = focus_cycle_ids_[i];
        app_ui::Widget* widget = root_ ? root_->FindById(id) : nullptr;
        if (widget && widget->Focused()) {
            focus_cycle_index_ = static_cast<int>(i);
            return;
        }
    }
    focus_cycle_index_ = 0;
}

bool DeviceSettingApp::HandleFocusCycle(const ButtonEvent &event) {
    if (!is_wifi_scene_ || focus_cycle_ids_.empty()) {
        return false;
    }

    const bool tab_focused = tabview_ && tabview_->Focused();
    if (tab_focused) {
        if (event.id != AppButton::Up && event.id != AppButton::Down) {
            return false;
        }
    } else if (event.id != AppButton::Left && event.id != AppButton::Right) {
        return false;
    }

    SyncFocusCycleIndex();
    const int count = static_cast<int>(focus_cycle_ids_.size());
    if (event.id == AppButton::Up || event.id == AppButton::Left) {
        focus_cycle_index_ = (focus_cycle_index_ + count - 1) % count;
    } else {
        focus_cycle_index_ = (focus_cycle_index_ + 1) % count;
    }
    ui_engine_.RequestFocus(focus_cycle_ids_[focus_cycle_index_]);
    return true;
}

bool DeviceSettingApp::HandleTabViewNav(AppContext &ctx, const ButtonEvent &event) {
    if (!tabview_ || !tabview_->Focused()) {
        return false;
    }
    if (event.id == AppButton::Left) {
        PrevScene(ctx);
        return true;
    }
    if (event.id == AppButton::Right) {
        NextScene(ctx);
        return true;
    }
    return false;
}

bool DeviceSettingApp::HandleKeyboardButtons(const ButtonEvent &event) {
    if (!keyboard_visible_) {
        return false;
    }

    if (event.id == AppButton::B) {
        DeletePasswordChar();
        return true;
    }
    if (event.id == AppButton::Select) {
        HideKeyboard();
        return true;
    }
    if (event.id == AppButton::Start) {
        if (event.action == ButtonAction::LongPress) {
            return true;
        }
        ConfirmPassword();
        return true;
    }

    app_ui::KeyCode key{};
    if (!MapButtonToKey(event.id, key)) {
        return true;
    }
    if (!keyboard_) {
        return true;
    }

    app_ui::InputEvent e;
    e.type = (event.action == ButtonAction::LongPress) ? app_ui::InputType::KeyRepeat : app_ui::InputType::KeyDown;
    e.key = static_cast<int>(key);
    e.timestamp = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    keyboard_->OnInput(e, app_ui::InputPhase::Target);
    return true;
}

bool DeviceSettingApp::HandleConfirmDialogButtons(const ButtonEvent &event) {
    if (!confirm_dialog_visible_) {
        return false;
    }

    if (event.id == AppButton::B || event.id == AppButton::Select) {
        HideConfirmDialog();
        return true;
    }

    if (event.id == AppButton::Up || event.id == AppButton::Down || event.id == AppButton::Left ||
        event.id == AppButton::Right) {
        if (button_submit_ && button_cancel_) {
            const bool submit_focused = button_submit_->Focused();
            ui_engine_.RequestFocus(submit_focused ? button_cancel_->Id() : button_submit_->Id());
        }
        RefreshHintAlertLabel();
        return true;
    }

    if (event.id == AppButton::C || event.id == AppButton::Start) {
        if (button_cancel_ && button_cancel_->Focused()) {
            HideConfirmDialog();
            return true;
        }

        const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
        if (confirm_connect_in_progress_ || now_us < confirm_cooldown_until_us_) {
            SetHintAlertLabel("正在连接中，请勿重复提交");
            return true;
        }

        ConfirmPendingDialogAction();
        return true;
    }

    return true;
}

bool DeviceSettingApp::HandleFocusedButtons(const ButtonEvent &event) {
    const bool action = (event.id == AppButton::Start || event.id == AppButton::C);
    if (!action) {
        return false;
    }
    if (scan_button_ && scan_button_->Focused()) {
        PopulateScanResults();
        return true;
    }
    return false;
}

bool DeviceSettingApp::HandleListViewActivate(const ButtonEvent &event) {
    const bool action = (event.id == AppButton::Start || event.id == AppButton::C || event.id == AppButton::D);
    if (!action) {
        return false;
    }

    if (scan_list_ && scan_list_->Focused()) {
        if (event.id == AppButton::D) {
            return true;
        }
        const int index = scan_list_->SelectedIndex();
        if (index < 0 || index >= static_cast<int>(scan_ssids_raw_.size())) {
            return true;
        }
        const auto& ssid = scan_ssids_raw_[index];
        if (ssid.empty() || IsPlaceholderItem(ssid)) {
            return true;
        }
        ShowKeyboardForSsid(ssid);
        return true;
    }

    if (saved_list_ && saved_list_->Focused()) {
        const int index = saved_list_->SelectedIndex();
        if (index < 0 || index >= static_cast<int>(saved_networks_.size())) {
            return true;
        }
        const auto& ssid = saved_networks_[index];
        if (!ssid.empty() && !IsPlaceholderItem(ssid)) {
            if (event.id == AppButton::D) {
                ShowConfirmDialog("确认删除该网络吗？", "", ssid, true);
            } else {
                ShowConfirmDialog("确认连接到该网络吗？", "", ssid, false);
            }
        }
        return true;
    }
    return false;
}

void DeviceSettingApp::SendInputToUi(const ButtonEvent &event) {
    app_ui::KeyCode key{};
    if (!MapButtonToKey(event.id, key)) {
        return;
    }

    app_ui::InputEvent e;
    e.type = (event.action == ButtonAction::LongPress) ? app_ui::InputType::KeyRepeat : app_ui::InputType::KeyDown;
    e.key = static_cast<int>(key);
    ui_engine_.OnInput(e);
}

void DeviceSettingApp::ShowKeyboardForSsid(const std::string& ssid) {
    StopAutoCloseDialog();
    HideConfirmDialog();

    keyboard_visible_ = true;
    active_ssid_ = ssid;
    password_input_.clear();

    if (keyboard_) {
        auto profile = keyboard_->Profile();
        profile.page = 0;
        keyboard_->SetProfile(profile);
        keyboard_->SetSelectedIndex(0);
        keyboard_->SetVisible(true);
    }
    if (input_dialog_) {
        input_dialog_->SetVisible(true);
    }
    if (ssid_label_) {
        ssid_label_->SetText(std::string("SSID名称：") + ssid);
        ssid_label_->SetVisible(true);
    }
    if (password_label_) {
        password_label_->SetText("输入密码：");
        password_label_->SetVisible(true);
    }
    if (password_area_) {
        password_area_->SetText("");
        password_area_->SetVisible(true);
    }
    SetInputStatus(kInputInitStatus);
    if (keyboard_) {
        ui_engine_.RequestFocus(keyboard_->Id());
    }
}

void DeviceSettingApp::ShowConfirmDialog(const std::string& title, const std::string& detail, const std::string& ssid,
                                         bool delete_action) {
    StopAutoCloseDialog();
    HideKeyboard();

    confirm_dialog_visible_ = true;
    confirm_delete_action_ = delete_action;
    pending_confirm_ssid_ = ssid;

    if (hint_dialog_) {
        hint_dialog_->SetVisible(true);
    }
    SetHintAlertLabel(detail.empty() ? title : (title + detail));

    if (button_submit_ && button_cancel_) {
        ui_engine_.RequestFocus(button_submit_->Id());
    }
}

void DeviceSettingApp::HideConfirmDialog() {
    confirm_dialog_visible_ = false;
    confirm_delete_action_ = false;
    pending_confirm_ssid_.clear();
    hint_alert_text_.clear();

    if (hint_dialog_) {
        hint_dialog_->SetVisible(false);
    }
    if (hint_alert_label_) {
        hint_alert_label_->SetVisible(false);
    }
}

void DeviceSettingApp::SetHintAlertLabel(const std::string& text) {
    hint_alert_text_ = text;
    RefreshHintAlertLabel();
}

void DeviceSettingApp::RefreshHintAlertLabel() {
    if (!hint_alert_label_ && hint_dialog_) {
        hint_alert_label_ = dynamic_cast<app_ui::LabelWidget*>(hint_dialog_->FindById(kWidgetHintAlert));
        if (!hint_alert_label_) {
            hint_alert_label_ = dynamic_cast<app_ui::LabelWidget*>(hint_dialog_->FindById(kWidgetPageAlert));
        }
        if (!hint_alert_label_) {
            for (const auto& child : hint_dialog_->Children()) {
                auto* label = dynamic_cast<app_ui::LabelWidget*>(child.get());
                if (label != nullptr) {
                    hint_alert_label_ = label;
                    break;
                }
            }
        }
    }
    if (!hint_alert_label_) {
        return;
    }
    if (hint_dialog_) {
        hint_dialog_->SetZOrder(25);
        hint_dialog_->SetVisible(confirm_dialog_visible_);
        hint_dialog_->MarkDirty();
    }
    hint_alert_label_->SetText(hint_alert_text_);
    hint_alert_label_->SetZOrder(30);
    hint_alert_label_->SetVisible(confirm_dialog_visible_);
    hint_alert_label_->MarkDirty();
}

void DeviceSettingApp::ConfirmPendingDialogAction() {
    if (!confirm_dialog_visible_ || pending_confirm_ssid_.empty()) {
        HideConfirmDialog();
        return;
    }

    if (confirm_delete_action_) {
        DeleteSavedSsid(pending_confirm_ssid_);
        HideConfirmDialog();
        return;
    }

    SetHintAlertLabel("网络连接中 60秒");
    confirm_connect_in_progress_ = true;
    confirm_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1200ULL * 1000ULL;

    const std::string password = GetSavedPasswordBySsid(pending_confirm_ssid_);
    if (password.empty()) {
        confirm_connect_in_progress_ = false;
        confirm_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1000ULL * 1000ULL;
        SetHintAlertLabel("连接失败: 未找到密码");
        return;
    }

    std::string error;
    if (!ConnectToSsidWithPassword(pending_confirm_ssid_, password, false, error, 60000, true, true)) {
        confirm_connect_in_progress_ = false;
        confirm_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1500ULL * 1000ULL;
        SetHintAlertLabel(std::string("连接失败: ") + error);
        return;
    }
    confirm_connect_in_progress_ = false;
    confirm_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1000ULL * 1000ULL;

    SetHintAlertLabel("连接成功");
    UpdateAlertStatusByNetwork();
    StartAutoCloseDialog(1000);
}

void DeviceSettingApp::HideKeyboard() {
    keyboard_visible_ = false;
    active_ssid_.clear();
    password_input_.clear();

    if (keyboard_) {
        keyboard_->SetVisible(false);
    }
    if (input_dialog_) {
        input_dialog_->SetVisible(false);
    }
    if (ssid_label_) {
        ssid_label_->SetVisible(false);
    }
    if (password_label_) {
        password_label_->SetVisible(false);
    }
    if (password_area_) {
        password_area_->SetVisible(false);
    }
}

void DeviceSettingApp::AppendPassword(const char* value) {
    if (!keyboard_visible_ || !value || !value[0]) {
        return;
    }
    password_input_ += value;
    UpdatePasswordText();
}

void DeviceSettingApp::DeletePasswordChar() {
    if (!keyboard_visible_) {
        return;
    }
    if (password_input_.empty()) {
        HideKeyboard();
        return;
    }
    password_input_.pop_back();
    UpdatePasswordText();
}

void DeviceSettingApp::ConfirmPassword() {
    if (!keyboard_visible_) {
        return;
    }
    if (active_ssid_.empty()) {
        return;
    }

    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    if (now_us < submit_cooldown_until_us_) {
        SetInputStatus("正在连接中，请勿重复提交");
        return;
    }

    if (submit_connect_in_progress_) {
        SetInputStatus("正在连接中，请勿重复提交");
        return;
    }
    if (password_input_.empty()) {
        SetInputStatus("请输入密码后再提交");
        return;
    }

    submit_connect_in_progress_ = true;
    submit_cooldown_until_us_ = now_us + 1200ULL * 1000ULL;
    SetInputStatus("网络正在连接中 60秒");
    std::string error;
    if (!ConnectToSsidWithPassword(active_ssid_, password_input_, true, error, 60000, true)) {
        submit_connect_in_progress_ = false;
        submit_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1500ULL * 1000ULL;
        SetInputStatus(error);
        return;
    }
    submit_connect_in_progress_ = false;
    submit_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1000ULL * 1000ULL;

    SetInputStatus("网络连接成功");
    UpdateAlertStatusByNetwork();
    StartAutoCloseDialog(1000);
}

void DeviceSettingApp::UpdatePasswordText() {
    if (password_area_) {
        password_area_->SetText(password_input_);
    }
}

bool DeviceSettingApp::ConnectToSsidWithPassword(const std::string& ssid,
                                                  const std::string& password,
                                                  bool save_on_success,
                                                  std::string& error,
                                                  int timeout_ms,
                                                  bool show_countdown,
                                                  bool countdown_on_hint_dialog) {
    error.clear();
    if (ssid.empty()) {
        error = "SSID为空";
        return false;
    }

    auto& ssid_manager = SsidManager::GetInstance();
    const auto original_list = ssid_manager.GetSsidList();

    auto restore_ssids = [&ssid_manager, &original_list]() {
        ssid_manager.Clear();
        for (auto it = original_list.rbegin(); it != original_list.rend(); ++it) {
            ssid_manager.AddSsid(it->ssid, it->password);
        }
    };

    ssid_manager.Clear();
    ssid_manager.AddSsid(ssid, password);

    auto& wifi = WifiManager::GetInstance();
    if (!wifi.Initialize()) {
        restore_ssids();
        error = "WiFi初始化失败";
        return false;
    }

    connect_wait_connected_ = false;
    connect_wait_failed_ = false;
    connect_wait_reason_ = static_cast<uint8_t>(WIFI_REASON_UNSPECIFIED);

    if (connect_disconnected_handler_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, connect_disconnected_handler_);
        connect_disconnected_handler_ = nullptr;
    }
    if (connect_got_ip_handler_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, connect_got_ip_handler_);
        connect_got_ip_handler_ = nullptr;
    }

    esp_event_handler_instance_register(
        WIFI_EVENT,
        WIFI_EVENT_STA_DISCONNECTED,
        &DeviceSettingApp::OnConnectWifiEvent,
        this,
        &connect_disconnected_handler_);
    esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &DeviceSettingApp::OnConnectIpEvent,
        this,
        &connect_got_ip_handler_);

    if (wifi.IsConnected()) {
        const std::string current_ssid = wifi.GetSsid();
        ESP_LOGI(kTag, "Stop current station connection (%s) before credential verification for %s",
                 current_ssid.c_str(), ssid.c_str());
        wifi.StopStation();
    }

    
    wifi.StartStation();

    const int effective_timeout_ms = (timeout_ms > 0) ? timeout_ms : 12000;
    const int step_ms = 100;
    int elapsed_ms = 0;
    int last_remain_sec = -1;
    bool connected = false;
    bool connected_to_other_ssid = false;
    std::string other_ssid;
    while (elapsed_ms < effective_timeout_ms) {
        if (show_countdown) {
            const int remain_sec = std::max(0, (effective_timeout_ms - elapsed_ms + 999) / 1000);
            if (remain_sec != last_remain_sec) {
                const std::string msg = std::string("网络正在连接中 ") + std::to_string(remain_sec) + "秒";
                if (countdown_on_hint_dialog) {
                    SetHintAlertLabel(msg);
                } else {
                    SetInputStatus(msg);
                }
                last_remain_sec = remain_sec;
            }
        }
        if (connect_wait_connected_ || wifi.IsConnected()) {
            const std::string current_ssid = wifi.GetSsid();
            if (!current_ssid.empty() && current_ssid == ssid) {
                connected = true;
                break;
            }
            if (!current_ssid.empty()) {
                connected_to_other_ssid = true;
                other_ssid = current_ssid;
            }
        }
        if (connect_wait_failed_) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        elapsed_ms += step_ms;
    }

    const bool keep_station_running = connected;
    if (!keep_station_running) {
        wifi.StopStation();
    }

    if (connect_disconnected_handler_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, connect_disconnected_handler_);
        connect_disconnected_handler_ = nullptr;
    }
    if (connect_got_ip_handler_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, connect_got_ip_handler_);
        connect_got_ip_handler_ = nullptr;
    }

    restore_ssids();

    if (connected) {
        if (save_on_success) {
            ssid_manager.AddSsid(ssid, password);
        }

        const auto& list = ssid_manager.GetSsidList();
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].ssid == ssid) {
                if (i != 0) {
                    ssid_manager.SetDefaultSsid(static_cast<int>(i));
                }
                break;
            }
        }
        last_connected_ssid_.clear();
        PopulateSavedNetworks();
        RefreshNetworkListDisplay();
        UpdateAlertStatusByNetwork();
        return true;
    }

    wifi.StartStation();
    if (connect_wait_failed_) {
        error = ConnectErrorToText(connect_wait_reason_);
    } else if (connected_to_other_ssid && !other_ssid.empty()) {
        error = std::string("连接到了其他网络: ") + other_ssid;
    } else {
        error = "连接超时";
    }
    return false;
}

void DeviceSettingApp::ConnectToSavedSsid(const std::string& ssid) {
    const std::string password = GetSavedPasswordBySsid(ssid);
    if (password.empty()) {
        return;
    }
    std::string error;
    (void)ConnectToSsidWithPassword(ssid, password, false, error);
}

void DeviceSettingApp::DeleteSavedSsid(const std::string& ssid) {
    if (ssid.empty() || IsPlaceholderItem(ssid)) {
        return;
    }

    auto& ssid_manager = SsidManager::GetInstance();
    const auto& list = ssid_manager.GetSsidList();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].ssid == ssid) {
            ssid_manager.RemoveSsid(static_cast<int>(i));
            break;
        }
    }
    PopulateSavedNetworks();
}

void DeviceSettingApp::StartAutoCloseDialog(int delay_ms) {
    if (delay_ms <= 0) {
        RunAutoCloseDialogNow();
        return;
    }

    if (dialog_auto_close_timer_ == nullptr) {
        esp_timer_create_args_t args = {
            .callback = &DeviceSettingApp::OnAutoCloseTimer,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "dev_setting_dialog_close",
            .skip_unhandled_events = true
        };
        if (esp_timer_create(&args, &dialog_auto_close_timer_) != ESP_OK) {
            dialog_auto_close_timer_ = nullptr;
            return;
        }
    }

    esp_timer_stop(dialog_auto_close_timer_);
    esp_timer_start_once(dialog_auto_close_timer_, static_cast<uint64_t>(delay_ms) * 1000ULL);
}

void DeviceSettingApp::StopAutoCloseDialog() {
    if (dialog_auto_close_timer_ != nullptr) {
        esp_timer_stop(dialog_auto_close_timer_);
        esp_timer_delete(dialog_auto_close_timer_);
        dialog_auto_close_timer_ = nullptr;
    }
}

void DeviceSettingApp::RunAutoCloseDialogNow() {
    HideKeyboard();
    HideConfirmDialog();
    UpdateBottomBarHintByFocus();
    ui_engine_.RequestRender();
}

bool DeviceSettingApp::IsScanningInProgress() const {
    return scanning_in_progress_;
}

bool DeviceSettingApp::IsInputDialogVisible() const {
    return input_dialog_ && input_dialog_->Visible();
}

bool DeviceSettingApp::IsHintDialogVisible() const {
    return hint_dialog_ && hint_dialog_->Visible();
}

std::string DeviceSettingApp::GetSavedPasswordBySsid(const std::string& ssid) const {
    const auto& list = SsidManager::GetInstance().GetSsidList();
    for (const auto& item : list) {
        if (item.ssid == ssid) {
            return item.password;
        }
    }
    return {};
}

std::string DeviceSettingApp::ConnectErrorToText(uint8_t reason) const {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "密码输入错误";
        case WIFI_REASON_NO_AP_FOUND:
            return "未找到该网络";
        case WIFI_REASON_ASSOC_FAIL:
            return "连接失败(关联失败)";
        default:
            return "连接失败";
    }
}

std::string DeviceSettingApp::CurrentConnectedSsid() const {
    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsConnected()) {
        return {};
    }
    return wifi.GetSsid();
}

bool DeviceSettingApp::IsPlaceholderItem(const std::string& item) const {
    return item == "(none)" || item == "(empty)" || item == "(scan failed)";
}

void DeviceSettingApp::OnConnectWifiEvent(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    (void)event_base;
    if (event_id != WIFI_EVENT_STA_DISCONNECTED || !arg || !event_data) {
        return;
    }

    auto* app = static_cast<DeviceSettingApp*>(arg);
    auto* disconnected = static_cast<wifi_event_sta_disconnected_t*>(event_data);
    app->connect_wait_reason_ = disconnected->reason;

    switch (disconnected->reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_ASSOC_FAIL:
            app->connect_wait_failed_ = true;
            break;
        default:
            break;
    }
}

void DeviceSettingApp::OnConnectIpEvent(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    (void)event_base;
    (void)event_data;
    if (event_id != IP_EVENT_STA_GOT_IP || !arg) {
        return;
    }
    auto* app = static_cast<DeviceSettingApp*>(arg);
    app->connect_wait_connected_ = true;
}

void DeviceSettingApp::OnAutoCloseTimer(void* arg) {
    auto* app = static_cast<DeviceSettingApp*>(arg);
    if (!app) {
        return;
    }
    AppService::GetInstance().Schedule([app]() {
        app->RunAutoCloseDialogNow();
    });
}

void DeviceSettingApp::OnKeyboardKey(app_ui::SoftKeyboardWidget* widget, const char* value, void* ctx) {
    (void)widget;
    auto* app = static_cast<DeviceSettingApp*>(ctx);
    if (!app) {
        return;
    }
    app->AppendPassword(value);
}

std::unique_ptr<AppBase> MakeDeviceSettingApp() {
    return std::make_unique<DeviceSettingApp>();
}
