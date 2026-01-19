#pragma once

#include <memory>

#include "eteacher/app_manager/app_base.h"

class FreeConversationApp : public AppBase {
public:
	FreeConversationApp();
	~FreeConversationApp() override;

	MenuMeta GetMenuMeta() const override;
	void OnEnter(AppContext &ctx) override;
	void OnExit(AppContext &ctx) override;
	void OnButton(AppContext &ctx, const ButtonEvent &event) override;
	void OnTick(AppContext &ctx, uint32_t delta_ms) override;

	virtual void OnChatMessage(const char* role, const char* content);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

std::unique_ptr<AppBase> MakeFreeConversationApp();
