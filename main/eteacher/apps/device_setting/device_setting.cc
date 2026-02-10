#include "eteacher/apps/device_setting/device_setting.h"

#include <algorithm>

#include <ssid_manager.h>
#include <wifi_manager.h>

#include <esp_err.h>
#include <esp_wifi.h>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_ui/input.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/apps/device_setting/device_setting_ui.h"

namespace {
constexpr const app_ui::desc::UiDesc* kUiDesc = &app_ui::generated::device_setting::kUi;

constexpr uint32_t kWidgetTabView = 0xA8EC2EDCu;
constexpr uint32_t kWidgetSoftKeyboard = 0xF2B83403u;
constexpr uint32_t kWidgetDialog = 0xA406E1D2u;
constexpr uint32_t kWidgetLabelSsid = 0x871C33F6u;
constexpr uint32_t kWidgetLabelPassword = 0x682DF257u;
constexpr uint32_t kWidgetTextArea = 0x08301078u;

constexpr uint32_t kWidgetScanButton = 0x54A65955u;
constexpr uint32_t kWidgetAddButton = 0x5686ECCDu;
constexpr uint32_t kWidgetScanList = 0xA7DCD964u;
constexpr uint32_t kWidgetSavedList = 0x0DB3CD15u;

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

void DeviceSettingApp::OnEnter(AppContext &ctx) {
	ui_ready_ = false;
	keyboard_visible_ = false;
	router_.Reset();
	scene_load_id_ = 0;
	epd_ = dynamic_cast<CustomEpdDisplay*>(ctx.board.GetDisplay());

	if (!LoadUi(ctx)) {
		return;
	}
	InitUiEngine();
	router_.SetActivateFn([this](AppContext& ctx, size_t /*index*/, const std::string& scene_id) {
		return LoadScene(ctx, scene_id, ++scene_load_id_);
	});
	if (router_.Activate(ctx, 0)) {
		Render(ctx);
	}
}

void DeviceSettingApp::OnExit(AppContext &ctx) {
	(void)ctx;
	ui_ready_ = false;
	keyboard_visible_ = false;
	epd_ = nullptr;
	router_.Reset();
	ui_engine_.Reset();
}

void DeviceSettingApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
	if (!ui_ready_ || !IsClickLike(event)) {
		return;
	}

	if (!keyboard_visible_ && HandleFocusCycle(event)) {
		Render(ctx);
		return;
	}

	if (HandleKeyboardButtons(event)) {
		Render(ctx);
		return;
	}
	if (HandleTabViewNav(ctx, event)) {
		Render(ctx);
		return;
	}
	if (HandleFocusedButtons(event)) {
		Render(ctx);
		return;
	}
	if (HandleListViewActivate(event)) {
		Render(ctx);
		return;
	}

	SendInputToUi(event);
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
	ui_engine_.SetRoot(std::move(new_root));
	UpdateTabSelection();
	BuildFocusCycle(scene_id);
	SyncFocusCycleIndex();
	if (tabview_) {
		ui_engine_.RequestFocus(tabview_->Id());
	}
	HideKeyboard();
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
		add_button_ = nullptr;
		scan_list_ = nullptr;
		saved_list_ = nullptr;
		keyboard_ = nullptr;
		dialog_ = nullptr;
		ssid_label_ = nullptr;
		password_label_ = nullptr;
		password_area_ = nullptr;
		return;
	}

	tabview_ = dynamic_cast<app_ui::TabViewWidget*>(root->FindById(kWidgetTabView));
	scan_button_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetScanButton));
	add_button_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetAddButton));
	scan_list_ = dynamic_cast<app_ui::ListViewWidget*>(root->FindById(kWidgetScanList));
	saved_list_ = dynamic_cast<app_ui::ListViewWidget*>(root->FindById(kWidgetSavedList));
	keyboard_ = dynamic_cast<app_ui::SoftKeyboardWidget*>(root->FindById(kWidgetSoftKeyboard));
	dialog_ = dynamic_cast<app_ui::DialogWidget*>(root->FindById(kWidgetDialog));
	ssid_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetLabelSsid));
	password_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetLabelPassword));
	password_area_ = dynamic_cast<app_ui::TextAreaWidget*>(root->FindById(kWidgetTextArea));

	if (keyboard_) {
		keyboard_->SetOnKey(&DeviceSettingApp::OnKeyboardKey, this);
	}
	if (tabview_) {
		tabview_->SetFocusable(true);
	}
	scan_results_.clear();
	scan_results_.push_back("(press scan)");
	if (scan_list_) {
		scan_list_->SetItems(scan_results_);
	}
	PopulateSavedNetworks();
	HideKeyboard();
}

void DeviceSettingApp::UpdateTabSelection() {
	if (!tabview_) {
		return;
	}
	tabview_->SetSelectedIndex(static_cast<int>(router_.Index()));
}

void DeviceSettingApp::PopulateScanResults() {
	scan_results_.clear();
	auto& wifi = WifiManager::GetInstance();
	(void)wifi.Initialize();
	wifi.StartStation();

	esp_err_t err = esp_wifi_scan_start(nullptr, true);
	if (err != ESP_OK) {
		scan_results_.push_back("(scan failed)");
		if (scan_list_) {
			scan_list_->SetItems(scan_results_);
		}
		return;
	}

	uint16_t ap_num = 0;
	err = esp_wifi_scan_get_ap_num(&ap_num);
	if (err != ESP_OK || ap_num == 0) {
		scan_results_.push_back("(none)");
		if (scan_list_) {
			scan_list_->SetItems(scan_results_);
		}
		return;
	}

	std::vector<wifi_ap_record_t> records(ap_num);
	uint16_t record_count = ap_num;
	err = esp_wifi_scan_get_ap_records(&record_count, records.data());
	if (err != ESP_OK || record_count == 0) {
		scan_results_.push_back("(none)");
		if (scan_list_) {
			scan_list_->SetItems(scan_results_);
		}
		return;
	}

	std::sort(records.begin(), records.end(), [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
		return a.rssi > b.rssi;
	});

	for (uint16_t i = 0; i < record_count; ++i) {
		const char* ssid = reinterpret_cast<const char*>(records[i].ssid);
		if (!ssid || ssid[0] == '\0') {
			continue;
		}
		scan_results_.push_back(ssid);
	}
	if (scan_results_.empty()) {
		scan_results_.push_back("(none)");
	}
	if (scan_list_) {
		scan_list_->SetItems(scan_results_);
	}
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
	if (saved_list_) {
		saved_list_->SetItems(saved_networks_);
	}
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
	if (event.id == AppButton::Start) {
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
	e.type = app_ui::InputType::KeyDown;
	e.key = static_cast<int>(key);
	keyboard_->OnInput(e, app_ui::InputPhase::Target);
	return true;
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
	AppendIfValid(add_button_, focus_cycle_ids_);
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
	// Fallback to tabview as the start of the cycle.
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
	} else {
		if (event.id != AppButton::Left && event.id != AppButton::Right) {
			return false;
		}
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

bool DeviceSettingApp::HandleFocusedButtons(const ButtonEvent &event) {
	const bool action = (event.id == AppButton::Start || event.id == AppButton::C);
	if (!action) {
		return false;
	}
	if (scan_button_ && scan_button_->Focused()) {
		PopulateScanResults();
		return true;
	}
	if (add_button_ && add_button_->Focused()) {
		PopulateSavedNetworks();
		return true;
	}
	return false;
}

bool DeviceSettingApp::HandleListViewActivate(const ButtonEvent &event) {
	const bool action = (event.id == AppButton::Start || event.id == AppButton::C);
	if (!action) {
		return false;
	}

	if (scan_list_ && scan_list_->Focused()) {
		const auto ssid = scan_list_->SelectedItem();
		if (!ssid.empty()) {
			ShowKeyboardForSsid(ssid);
		}
		return true;
	}
	if (saved_list_ && saved_list_->Focused()) {
		const auto ssid = saved_list_->SelectedItem();
		if (!ssid.empty()) {
			ShowKeyboardForSsid(ssid);
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
	keyboard_visible_ = true;
	active_ssid_ = ssid;
	password_input_.clear();

	if (keyboard_) {
		keyboard_->SetPage(0);
		keyboard_->SetVisible(true);
	}
	if (dialog_) {
		dialog_->SetVisible(true);
	}
	if (ssid_label_) {
		ssid_label_->SetText(std::string("SSID: ") + ssid);
		ssid_label_->SetVisible(true);
	}
	if (password_label_) {
		password_label_->SetText("Password");
		password_label_->SetVisible(true);
	}
	if (password_area_) {
		password_area_->SetText("");
		password_area_->SetVisible(true);
	}
}

void DeviceSettingApp::HideKeyboard() {
	keyboard_visible_ = false;
	active_ssid_.clear();
	password_input_.clear();

	if (keyboard_) {
		keyboard_->SetVisible(false);
	}
	if (dialog_) {
		dialog_->SetVisible(false);
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
	if (!active_ssid_.empty() && !password_input_.empty()) {
		SsidManager::GetInstance().AddSsid(active_ssid_, password_input_);
		PopulateSavedNetworks();
	}
	HideKeyboard();
}

void DeviceSettingApp::UpdatePasswordText() {
	if (password_area_) {
		password_area_->SetText(password_input_);
	}
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
