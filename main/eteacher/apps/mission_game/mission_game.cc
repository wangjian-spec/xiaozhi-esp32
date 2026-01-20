#include "eteacher/apps/mission_game/mission_game.h"
#include "display.h"

MenuMeta MissionGameApp::GetMenuMeta() const
{
    return MenuMeta{"mission_game", "任务游戏", ""};
}

void MissionGameApp::OnEnter(AppContext &ctx)
{
    ctx.board.GetDisplay()->SetChatMessage("system", "Enter Mission Game");
}

void MissionGameApp::OnExit(AppContext &ctx)
{
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Mission Game");
}

void MissionGameApp::OnButton(AppContext &ctx, const ButtonEvent &event)
{
    (void)event;
    ctx.board.GetDisplay()->SetChatMessage("system", "Mission Game (stub)");
}

std::unique_ptr<AppBase> MakeMissionGameApp()
{
    return std::make_unique<MissionGameApp>();
}
