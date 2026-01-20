#include "eteacher/apps/words_game/words_game.h"
#include "display.h"

MenuMeta WordsGameApp::GetMenuMeta() const
{
    return MenuMeta{"words_game", "单词游戏", ""};
}

void WordsGameApp::OnEnter(AppContext &ctx)
{
    ctx.board.GetDisplay()->SetChatMessage("system", "Enter Words Game");
}

void WordsGameApp::OnExit(AppContext &ctx)
{
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Words Game");
}

void WordsGameApp::OnButton(AppContext &ctx, const ButtonEvent &event)
{
    (void)event;
    ctx.board.GetDisplay()->SetChatMessage("system", "Words Game (stub)");
}

std::unique_ptr<AppBase> MakeWordsGameApp()
{
    return std::make_unique<WordsGameApp>();
}
