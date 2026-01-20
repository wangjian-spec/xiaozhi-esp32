#include "eteacher/apps/txt_reader/txt_reader.h"
#include "display.h"

MenuMeta TxtReaderApp::GetMenuMeta() const
{
    return MenuMeta{"txt_reader", "文本阅读", ""};
}

void TxtReaderApp::OnEnter(AppContext &ctx)
{
    ctx.board.GetDisplay()->SetChatMessage("system", "Enter Txt Reader");
}

void TxtReaderApp::OnExit(AppContext &ctx)
{
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Txt Reader");
}

void TxtReaderApp::OnButton(AppContext &ctx, const ButtonEvent &event)
{
    (void)event;
    ctx.board.GetDisplay()->SetChatMessage("system", "Txt Reader (stub)");
}

std::unique_ptr<AppBase> MakeTxtReaderApp()
{
    return std::make_unique<TxtReaderApp>();
}
