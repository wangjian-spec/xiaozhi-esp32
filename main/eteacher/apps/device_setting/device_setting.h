#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app_manager/app_base.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/ui_engine.h"
#include "eteacher/app_ui/ui_router.h"
#include "eteacher/app_ui/widget.h"

class CustomEpdDisplay;

class DeviceSettingApp : public AppBase {
public:
	DeviceSettingApp() = default;

	MenuMeta GetMenuMeta() const override;
	void OnEnter(AppContext &ctx) override;
	void OnExit(AppContext &ctx) override;
	void OnButton(AppContext &ctx, const ButtonEvent &event) override;

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
	void BuildFocusCycle(const std::string& scene_id);
	void SyncFocusCycleIndex();
	bool HandleFocusCycle(const ButtonEvent &event);

	bool HandleTabViewNav(AppContext &ctx, const ButtonEvent &event);
	bool HandleKeyboardButtons(const ButtonEvent &event);
	bool HandleFocusedButtons(const ButtonEvent &event);
	bool HandleListViewActivate(const ButtonEvent &event);
	void SendInputToUi(const ButtonEvent &event);

	void ShowKeyboardForSsid(const std::string& ssid);
	void HideKeyboard();
	void AppendPassword(const char* value);
	void DeletePasswordChar();
	void ConfirmPassword();
	void UpdatePasswordText();

	static void OnKeyboardKey(app_ui::SoftKeyboardWidget* widget, const char* value, void* ctx);

	app_ui::UIEngine ui_engine_{};
	app_ui::runtime::SceneRuntime scene_runtime_{};
	UiRouter router_{};
	uint16_t scene_load_id_ = 0;
	CustomEpdDisplay* epd_ = nullptr;
	bool ui_ready_ = false;
	bool keyboard_visible_ = false;
	bool is_wifi_scene_ = false;

	std::string active_ssid_{};
	std::string password_input_{};
	std::vector<std::string> scan_results_{};
	std::vector<std::string> saved_networks_{};
	std::vector<uint32_t> focus_cycle_ids_{};
	int focus_cycle_index_ = 0;

	app_ui::Widget* root_ = nullptr;
	app_ui::TabViewWidget* tabview_ = nullptr;
	app_ui::ButtonWidget* scan_button_ = nullptr;
	app_ui::ButtonWidget* add_button_ = nullptr;
	app_ui::ListViewWidget* scan_list_ = nullptr;
	app_ui::ListViewWidget* saved_list_ = nullptr;
	app_ui::SoftKeyboardWidget* keyboard_ = nullptr;
	app_ui::DialogWidget* dialog_ = nullptr;
	app_ui::LabelWidget* ssid_label_ = nullptr;
	app_ui::LabelWidget* password_label_ = nullptr;
	app_ui::TextAreaWidget* password_area_ = nullptr;
};

std::unique_ptr<AppBase> MakeDeviceSettingApp();
