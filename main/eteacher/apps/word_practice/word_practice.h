#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app_manager/app_base.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/ui_engine.h"
#include "eteacher/app_ui/ui_router.h"

class CustomEpdDisplay;

// Demo app: cycle words and mark as practiced.
class WordPracticeApp : public AppBase {
public:
	WordPracticeApp();

	MenuMeta GetMenuMeta() const override;
	void OnEnter(AppContext &ctx) override;
	void OnExit(AppContext &ctx) override;
	void OnButton(AppContext &ctx, const ButtonEvent &event) override;

private:
	bool LoadUi(AppContext &ctx);
	void InitUiEngine();
	void HandleAppLevelKeys(AppContext &ctx, const ButtonEvent &event);
	bool LoadScene(AppContext &ctx, const std::string& scene_id, uint16_t scene_index);

	void Render(AppContext &ctx);
	void PrevScene(AppContext &ctx);
	void NextScene(AppContext &ctx);

	std::vector<std::string> words_;
	int index_ = 0;

	app_ui::UIEngine ui_engine_{};
	app_ui::runtime::SceneRuntime scene_runtime_{};
	UiRouter router_{};
	uint16_t scene_load_id_ = 0;
	CustomEpdDisplay* epd_ = nullptr;
	bool ui_ready_ = false;
};

std::unique_ptr<AppBase> MakeWordPracticeApp();
