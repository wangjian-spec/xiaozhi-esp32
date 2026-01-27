#include "eteacher/apps/words_book/words_book.h"
#include "display.h"

MenuMeta WordsBookApp::GetMenuMeta() const
{
    return MenuMeta{"words_book", "单词本", ""};
}

void WordsBookApp::OnEnter(AppContext &ctx)
{
    ctx.board.GetDisplay()->SetChatMessage("system", "Enter Words Book");
}

void WordsBookApp::OnExit(AppContext &ctx)
{
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Words Book");
}

void WordsBookApp::OnButton(AppContext &ctx, const ButtonEvent &event)
{
    (void)event;
    ctx.board.GetDisplay()->SetChatMessage("system", "Words Book (stub)");
}

std::unique_ptr<AppBase> MakeWordsBookApp()
{
    return std::make_unique<WordsBookApp>();
}
