#include "eteacher/apps/free_conversation.h"

#include "display.h"

static constexpr const char *kTitle = "Free Conversation";
static constexpr const char *kSubtitle = "Start to talk";

FreeConversationApp::FreeConversationApp() = default;

MenuMeta FreeConversationApp::GetMenuMeta() const
{
    return MenuMeta{ "free_conversation", "自由对话", "开始对话" };
}

void FreeConversationApp::OnEnter(AppContext &ctx)
{
    auto display = ctx.board.GetDisplay();
    display->SetStatus("自由对话");
    display->SetChatMessage("system", "按 Start 开始对话。按 Select 退出。");
    listening_ = false;
}

void FreeConversationApp::OnExit(AppContext &ctx)
{
    if (listening_)
    {
        ctx.board.GetDisplay()->SetChatMessage("system", "Stopped.");
    }
    listening_ = false;
}

void FreeConversationApp::OnButton(AppContext &ctx, const ButtonEvent &event)
{
    auto display = ctx.board.GetDisplay();
    if (event.id == AppButton::Start)
    {
        listening_ = !listening_;
        display->SetChatMessage("system", listening_ ? "Listening..." : "Stopped.");
        return;
    }

    // Other buttons ignored; Select handled by AppManager.
}

std::unique_ptr<AppBase> MakeFreeConversationApp()
{
    return std::make_unique<FreeConversationApp>();
}
