#include "eteacher/apps/free_conversation.h"

#include "display.h"

static constexpr const char *kTitle = "Free Conversation";
static constexpr const char *kSubtitle = "Hold PTT to talk";

FreeConversationApp::FreeConversationApp() = default;

MenuMeta FreeConversationApp::GetMenuMeta() const
{
    return MenuMeta{ "free_conversation", kTitle, kSubtitle };
}

void FreeConversationApp::OnEnter(AppContext &ctx)
{
    auto display = ctx.board.GetDisplay();
    display->SetStatus(kTitle);
    display->SetChatMessage("system", "Press/hold PTT to talk. Back to exit.");
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
    if (event.id == AppButton::Ptt || event.id == AppButton::PttAlt)
    {
        listening_ = !listening_;
        display->SetChatMessage("system", listening_ ? "Listening..." : "Stopped.");
        return;
    }

    // Other buttons ignored; Back handled by AppManager.
}

std::unique_ptr<AppBase> MakeFreeConversationApp()
{
    return std::make_unique<FreeConversationApp>();
}
