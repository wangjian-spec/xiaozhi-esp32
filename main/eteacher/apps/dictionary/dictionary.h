#pragma once

#include <memory>
#include <string>
#include <vector>

#include "eteacher/app_manager/app_base.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/ui_engine.h"
#include "eteacher/app_ui/ui_router.h"
#include "eteacher/app_ui/widget.h"

class CustomEpdDisplay;

class DictionaryApp : public AppBase {
public:
	DictionaryApp() = default;

	MenuMeta GetMenuMeta() const override;
	void OnEnter(AppContext &ctx) override;
	void OnExit(AppContext &ctx) override;
	void OnButton(AppContext &ctx, const ButtonEvent &event) override;
	bool ShouldInterceptSelectExit() const override;

private:
	struct EntryData {
		bool found = false;
		std::vector<std::pair<std::string, std::string>> fields;
	};

	bool LoadUi(AppContext &ctx);
	bool LoadScene(AppContext &ctx, const std::string &scene_id, uint16_t scene_index);
	void InitUiEngine();
	void Render(AppContext &ctx);
	void BindWidgets(app_ui::Widget *root);

	void ShowKeyboard();
	void HideKeyboard(bool from_select = false);
	void AppendInput(const char *value);
	void DeleteInputChar();

	void QueryCurrentWordAndDisplay();
	EntryData QueryByWord(const std::string &word) const;
	void UpdateResultFrame(const EntryData &entry);
	void SetStatusText(const std::string &text);
	void UpdateBottomBarHintByFocus();
	void TryAutoShowKeyboardByFocus();
	bool IsWordInBook(const std::string &word) const;
	bool AddWordToBook(const std::string &word) const;
	bool RemoveWordFromBook(const std::string &word) const;

	static void OnKeyboardKey(app_ui::SoftKeyboardWidget *widget, const char *value, void *ctx);

	app_ui::UIEngine ui_engine_{};
	app_ui::runtime::SceneRuntime scene_runtime_{};
	UiRouter router_{};
	uint16_t scene_load_id_ = 0;
	CustomEpdDisplay *epd_ = nullptr;
	bool ui_ready_ = false;
	bool keyboard_visible_ = false;
	bool suppress_auto_keyboard_ = false;

	app_ui::Widget *root_ = nullptr;
	app_ui::TextAreaWidget *textarea_word_ = nullptr;
	app_ui::ButtonWidget *button_add_ = nullptr;
	app_ui::LabelWidget *label_status_ = nullptr;
	app_ui::FrameWidget *frame_result_ = nullptr;
	app_ui::BottomBarWidget *bottom_bar_ = nullptr;
	app_ui::SoftKeyboardWidget *keyboard_ = nullptr;

	std::string current_word_{};
	bool current_word_found_ = false;
};

std::unique_ptr<AppBase> MakeDictionaryApp();
