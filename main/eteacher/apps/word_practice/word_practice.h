#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app_manager/app_base.h"

// Demo app: cycle words and mark as practiced.
class WordPracticeApp : public AppBase {
public:
	WordPracticeApp();

	MenuMeta GetMenuMeta() const override;
	void OnEnter(AppContext &ctx) override;
	void OnExit(AppContext &ctx) override;
	void OnButton(AppContext &ctx, const ButtonEvent &event) override;

private:
	void Render(AppContext &ctx);

	std::vector<std::string> words_;
	int index_ = 0;
};

std::unique_ptr<AppBase> MakeWordPracticeApp();
