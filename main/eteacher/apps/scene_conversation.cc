#include "eteacher/apps/scene_conversation.h"

#include "display.h"

SceneConversationApp::SceneConversationApp()
{
    scenes_ = {"airport", "restaurant", "hotel", "shopping"};
}

MenuMeta SceneConversationApp::GetMenuMeta() const
{
    return MenuMeta{"scene_conversation", "Scene Conversation", "Up/Down select"};
}

void SceneConversationApp::OnEnter(AppContext &ctx)
{
    running_ = false;
    Render(ctx);
}

void SceneConversationApp::OnExit(AppContext &ctx)
{
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Scene");
    running_ = false;
}

void SceneConversationApp::OnButton(AppContext &ctx, const ButtonEvent &event)
{
    if (event.id == AppButton::Up)
    {
        index_ = (index_ - 1 + static_cast<int>(scenes_.size())) % static_cast<int>(scenes_.size());
        Render(ctx);
        return;
    }
    if (event.id == AppButton::Down)
    {
        index_ = (index_ + 1) % static_cast<int>(scenes_.size());
        Render(ctx);
        return;
    }
    if (event.id == AppButton::Select)
    {
        running_ = true;
        ctx.board.GetDisplay()->SetChatMessage("system", ("Scene: " + scenes_[index_] + "\nSpeak or PTT").c_str());
        return;
    }
    if ((event.id == AppButton::Ptt || event.id == AppButton::PttAlt) && running_)
    {
        ctx.board.GetDisplay()->SetChatMessage("system", "Answering...");
    }
}

void SceneConversationApp::Render(AppContext &ctx)
{
    auto display = ctx.board.GetDisplay();
    std::string msg = "Scenes:\n";
    for (size_t i = 0; i < scenes_.size(); ++i)
    {
        msg += (static_cast<int>(i) == index_) ? "> " : "  ";
        msg += scenes_[i];
        if (i + 1 < scenes_.size())
            msg += "\n";
    }
    display->SetChatMessage("system", msg.c_str());
}

std::unique_ptr<AppBase> MakeSceneConversationApp()
{
    return std::make_unique<SceneConversationApp>();
}
