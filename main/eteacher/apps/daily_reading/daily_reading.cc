#include "eteacher/apps/daily_reading/daily_reading.h"
#include "display.h"

MenuMeta DailyReadingApp::GetMenuMeta() const
{
    return MenuMeta{"daily_reading", "每日阅读", ""};
}

void DailyReadingApp::OnEnter(AppContext &ctx)
{
    ctx.board.GetDisplay()->SetChatMessage("system", "Enter Daily Reading");
}

void DailyReadingApp::OnExit(AppContext &ctx)
{
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Daily Reading");
}

void DailyReadingApp::OnButton(AppContext &ctx, const ButtonEvent &event)
{
    (void)event;
    ctx.board.GetDisplay()->SetChatMessage("system", "Daily Reading (stub)");
}

std::unique_ptr<AppBase> MakeDailyReadingApp()
{
    return std::make_unique<DailyReadingApp>();
}
