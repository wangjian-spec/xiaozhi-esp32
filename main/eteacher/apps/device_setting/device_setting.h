#pragma once

#include <memory>
#include <string>
#include <vector>

#include <esp_event.h>
#include <esp_timer.h>
#include <esp_wifi_types_generic.h>

#include "app_manager/app_base.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/ui_engine.h"
#include "eteacher/app_ui/ui_router.h"
#include "eteacher/app_ui/widget.h"

class CustomEpdDisplay;
#include "eteacher/apps/device_setting/et_server_client.h"

class DeviceSettingApp : public AppBase {
public:
	DeviceSettingApp() = default;

	MenuMeta GetMenuMeta() const override;
	void OnEnter(AppContext &ctx) override;
	void OnExit(AppContext &ctx) override;
	void OnButton(AppContext &ctx, const ButtonEvent &event) override;
	void OnTick(AppContext &ctx) override;
	bool ShouldInterceptSelectExit() const override;

private:
	bool LoadUi(AppContext &ctx);
	bool LoadScene(AppContext &ctx, const std::string& scene_id, uint16_t scene_index);
	void InitUiEngine();
	void Render(AppContext &ctx);
	void PrevScene(AppContext &ctx);
	void NextScene(AppContext &ctx);

	void BindWidgets(app_ui::Widget* root);
	void UpdateTabSelection();
	void PopulateScanResults();
	void PopulateSavedNetworks();
	void RefreshNetworkListDisplay();
	void UpdateAlertStatusByNetwork();
	void SetAlertLabel(const std::string& text);
	void SetInputStatus(const std::string& text);
	void SetBottomBarHint(const std::string& text);
	void PushBottomBarHint(const std::string& text);
	void UpdateBottomBarHintByFocus();
	void BuildFocusCycle(const std::string& scene_id);
	void SyncFocusCycleIndex();
	bool HandleFocusCycle(const ButtonEvent &event);

	bool HandleTabViewNav(AppContext &ctx, const ButtonEvent &event);
	bool HandleKeyboardButtons(const ButtonEvent &event);
	bool HandleConfirmDialogButtons(const ButtonEvent &event);
	bool HandleFocusedButtons(const ButtonEvent &event);
	bool HandleListViewActivate(const ButtonEvent &event);
	void SendInputToUi(const ButtonEvent &event);

	void ShowKeyboardForSsid(const std::string& ssid);
	void ShowKeyboardForUserField(app_ui::TextAreaWidget* field);
	void ShowConfirmDialog(const std::string& title, const std::string& detail, const std::string& ssid, bool delete_action);
	void HideConfirmDialog();
	void SetHintAlertLabel(const std::string& text);
	void RefreshHintAlertLabel();
	void ConfirmPendingDialogAction();
	void RefreshUserSettingsPage();
	void RefreshDeviceInfoPage();
	void RefreshClientUiState();
	void TriggerStatusCheck();
	void TriggerSendVerificationCode();
	void TriggerCompleteLogin();
	void TriggerResourceDownload();
	bool ConnectToSsidWithPassword(const std::string& ssid,
	                              const std::string& password,
	                              bool save_on_success,
	                              std::string& error,
	                              int timeout_ms = 12000,
	                              bool show_countdown = false,
	                              bool countdown_on_hint_dialog = false);
	void ConnectToSavedSsid(const std::string& ssid);
	void DeleteSavedSsid(const std::string& ssid);
	void HideKeyboard();
	void AppendPassword(const char* value);
	void DeletePasswordChar();
	void ConfirmPassword();
	void UpdatePasswordText();
	void StartAutoCloseDialog(int delay_ms);
	void StopAutoCloseDialog();
	void RunAutoCloseDialogNow();
	bool IsScanningInProgress() const;
	bool IsInputDialogVisible() const;
	bool IsHintDialogVisible() const;
	bool IsUserSettingsScene() const;
	bool IsDeviceInfoScene() const;
	std::string GetSavedPasswordBySsid(const std::string& ssid) const;
	std::string ConnectErrorToText(uint8_t reason) const;
	std::string CurrentConnectedSsid() const;
	bool IsPlaceholderItem(const std::string& item) const;
	std::string TextAreaText(app_ui::TextAreaWidget* field) const;
	void SetTextAreaText(app_ui::TextAreaWidget* field, const std::string& text);
	std::string UserFieldTitle(app_ui::TextAreaWidget* field) const;

	static void OnConnectWifiEvent(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
	static void OnConnectIpEvent(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
	static void OnAutoCloseTimer(void* arg);
	static void OnKeyboardKey(app_ui::SoftKeyboardWidget* widget, const char* value, void* ctx);

	enum class KeyboardMode {
		None,
		WifiPassword,
		UserField,
	};

	app_ui::UIEngine ui_engine_{};
	app_ui::runtime::SceneRuntime scene_runtime_{};
	UiRouter router_{};
	uint16_t scene_load_id_ = 0;
	CustomEpdDisplay* epd_ = nullptr;
	bool ui_ready_ = false;
	bool keyboard_visible_ = false;
	bool confirm_dialog_visible_ = false;
	bool confirm_delete_action_ = false;
	bool scanning_in_progress_ = false;
	bool has_scanned_once_ = false;
	bool last_scan_failed_ = false;
	bool connect_wait_connected_ = false;
	bool connect_wait_failed_ = false;
	bool submit_connect_in_progress_ = false;
	bool confirm_connect_in_progress_ = false;
	uint64_t submit_cooldown_until_us_ = 0;
	uint64_t confirm_cooldown_until_us_ = 0;
	uint8_t connect_wait_reason_ = WIFI_REASON_UNSPECIFIED;
	bool is_wifi_scene_ = false;
	KeyboardMode keyboard_mode_ = KeyboardMode::None;
	bool keyboard_ignore_activation_once_ = false;

	std::string active_ssid_{};
	std::string keyboard_field_title_{};
	std::string pending_confirm_ssid_{};
	std::string hint_alert_text_{};
	std::string bottom_bar_hint_{};
	std::string password_input_{};
	std::vector<std::string> scan_ssids_raw_{};
	std::vector<int> scan_rssi_raw_{};
	std::vector<std::string> scan_results_{};
	std::vector<std::string> saved_networks_{};
	std::vector<std::string> saved_display_items_{};
	std::string last_alert_text_{};
	std::string last_connected_ssid_{};
	std::vector<uint32_t> focus_cycle_ids_{};
	int focus_cycle_index_ = 0;

	app_ui::Widget* root_ = nullptr;
	app_ui::TabViewWidget* tabview_ = nullptr;
	app_ui::ButtonWidget* scan_button_ = nullptr;
	app_ui::ListViewWidget* scan_list_ = nullptr;
	app_ui::ListViewWidget* saved_list_ = nullptr;
	app_ui::BottomBarWidget* bottom_bar_ = nullptr;
	app_ui::SoftKeyboardWidget* keyboard_ = nullptr;
	app_ui::DialogWidget* input_dialog_ = nullptr;
	app_ui::DialogWidget* hint_dialog_ = nullptr;
	app_ui::ButtonWidget* button_submit_ = nullptr;
	app_ui::ButtonWidget* button_cancel_ = nullptr;
	app_ui::LabelWidget* ssid_label_ = nullptr;
	app_ui::LabelWidget* password_label_ = nullptr;
	app_ui::LabelWidget* status_info_label_ = nullptr;
	app_ui::LabelWidget* page_alert_label_ = nullptr;
	app_ui::LabelWidget* hint_alert_label_ = nullptr;
	app_ui::TextAreaWidget* password_area_ = nullptr;
	app_ui::TextAreaWidget* user_phone_area_ = nullptr;
	app_ui::TextAreaWidget* user_password_area_ = nullptr;
	app_ui::TextAreaWidget* user_code_area_ = nullptr;
	app_ui::TextAreaWidget* active_user_input_ = nullptr;
	app_ui::ButtonWidget* user_send_code_button_ = nullptr;
	app_ui::ButtonWidget* user_login_button_ = nullptr;
	app_ui::LabelWidget* user_status_label_ = nullptr;
	app_ui::LabelWidget* user_name_label_ = nullptr;
	app_ui::LabelWidget* user_phone_label_ = nullptr;
	app_ui::LabelWidget* user_mode_label_ = nullptr;
	app_ui::ButtonWidget* device_status_button_ = nullptr;
	app_ui::ButtonWidget* device_download_button_ = nullptr;
	app_ui::LabelWidget* device_model_label_ = nullptr;
	app_ui::LabelWidget* device_id_label_ = nullptr;
	app_ui::LabelWidget* device_resource_label_ = nullptr;
	app_ui::LabelWidget* device_activation_label_ = nullptr;
	EtServerClient et_server_client_{};

	esp_event_handler_instance_t connect_disconnected_handler_ = nullptr;
	esp_event_handler_instance_t connect_got_ip_handler_ = nullptr;
	esp_timer_handle_t dialog_auto_close_timer_ = nullptr;
};

std::unique_ptr<AppBase> MakeDeviceSettingApp();
