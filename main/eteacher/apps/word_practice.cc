// (empty)
#include "eteacher/apps/word_practice.h"

#include "display.h"

WordPracticeApp::WordPracticeApp()
{
	words_ = {"apple", "banana", "cat", "dog", "elephant"};
}

MenuMeta WordPracticeApp::GetMenuMeta() const
{
	return MenuMeta{"word_practice", "单词练习", "Up/Down to browse"};
}

void WordPracticeApp::OnEnter(AppContext &ctx)
{
	Render(ctx);
}

void WordPracticeApp::OnExit(AppContext &ctx)
{
	ctx.board.GetDisplay()->SetChatMessage("system", "Exit Word Practice");
}

void WordPracticeApp::OnButton(AppContext &ctx, const ButtonEvent &event)
{
	if (event.id == AppButton::Up)
	{
		index_ = (index_ - 1 + static_cast<int>(words_.size())) % static_cast<int>(words_.size());
		Render(ctx);
		return;
	}
	if (event.id == AppButton::Down)
	{
		index_ = (index_ + 1) % static_cast<int>(words_.size());
		Render(ctx);
		return;
	}
	if (event.id == AppButton::Start)
	{
		std::string msg = "Practice: " + words_[index_];
		ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
		return;
	}
}

void WordPracticeApp::Render(AppContext &ctx)
{
	auto display = ctx.board.GetDisplay();
	std::string msg = "Words:\n";
	for (size_t i = 0; i < words_.size(); ++i)
	{
		msg += (static_cast<int>(i) == index_) ? "> " : "  ";
		msg += words_[i];
		if (i + 1 < words_.size())
			msg += "\n";
	}
	display->SetChatMessage("system", msg.c_str());
}

std::unique_ptr<AppBase> MakeWordPracticeApp()
{
	return std::make_unique<WordPracticeApp>();
}
