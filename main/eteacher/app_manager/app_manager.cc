#include <esp_log.h>
#include "application.h"
#include "board.h"
#include "display.h"
#include "eteacher/app_manager/app_manager.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "eteacher/epd_manager/epd_renderer.h"
#include "eteacher/epd_manager/screen.h"

static const char *TAG = "AppManager";

AppManager &AppManager::GetInstance()
{
    static AppManager inst;
    return inst;
}

void AppManager::Init()
{
    if (inited_)
    {
        return;
    }
    inited_ = true;

    auto &buttons = ButtonManager::GetInstance();
    buttons.RegisterCallback(ButtonId::MENU_UP, []()
                             { AppManager::GetInstance().HandleButton(ButtonId::MENU_UP); });
    buttons.RegisterCallback(ButtonId::MENU_DOWN, []()
                             { AppManager::GetInstance().HandleButton(ButtonId::MENU_DOWN); });
    buttons.RegisterCallback(ButtonId::SELECT, []()
                             { AppManager::GetInstance().HandleButton(ButtonId::SELECT); });
    buttons.RegisterCallback(ButtonId::BACK, []()
                             { AppManager::GetInstance().HandleButton(ButtonId::BACK); });

    // Built-in apps adapted from the reference project layout.
    Register(std::make_shared<ActionApp>("ai_chat", "AI Chat", []()
                                         {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus("AI Chat");
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "Press PTT or wake word to talk."); }));

    Register(std::make_shared<ActionApp>("word_practice", "Word Practice", []()
                                         {
        ButtonManager::GetInstance().SetActiveScreen(ScreenId::WORD_PRACTICE);
        EpdManager::GetInstance().SetActiveScreen(static_cast<int>(ScreenId::WORD_PRACTICE));
        EpdManager::GetInstance().ShowMainMenu(); }, []()
                                         {
        ButtonManager::GetInstance().SetActiveScreen(ScreenId::MAIN);
        EpdManager::GetInstance().SetActiveScreen(static_cast<int>(ScreenId::MAIN)); }));

    Register(std::make_shared<ActionApp>("free_conversation", "Free Conversation", []()
                                         {
        ButtonManager::GetInstance().SetActiveScreen(ScreenId::FREE_CONVERSATION);
        EpdManager::GetInstance().SetActiveScreen(static_cast<int>(ScreenId::FREE_CONVERSATION));
        EpdManager::GetInstance().ShowMainMenu(); }, []()
                                         {
        ButtonManager::GetInstance().SetActiveScreen(ScreenId::MAIN);
        EpdManager::GetInstance().SetActiveScreen(static_cast<int>(ScreenId::MAIN)); }));

    Register(std::make_shared<ActionApp>("settings", "Settings", []()
                                         {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus("Settings");
        display->SetChatMessage("system", "Use Up/Down to select items."); }));

    ShowHome();
    EnsureUiTask();
}

void AppManager::Register(std::shared_ptr<AppBase> app)
{
    if (!app || !app->show_in_list())
    {
        return;
    }
    apps_.push_back(std::move(app));
    if (selected_index_ < 0)
    {
        selected_index_ = 0;
    }
    if (selected_index_ >= static_cast<int>(apps_.size()))
    {
        selected_index_ = static_cast<int>(apps_.size()) - 1;
    }
}

void AppManager::ShowHome()
{
    RenderHome();
}

void AppManager::HandleButton(ButtonId id)
{
    if (apps_.empty())
    {
        return;
    }

    if (current_)
    {
        if (id == ButtonId::BACK)
        {
            ExitCurrent();
        }
        return;
    }

    switch (id)
    {
    case ButtonId::MENU_UP:
    {
        selected_index_ = (selected_index_ - 1 + static_cast<int>(apps_.size())) % static_cast<int>(apps_.size());
        RenderHome();
        break;
    }
    case ButtonId::MENU_DOWN:
    {
        selected_index_ = (selected_index_ + 1) % static_cast<int>(apps_.size());
        RenderHome();
        break;
    }
    case ButtonId::SELECT:
    {
        EnterSelected();
        break;
    }
    default:
        break;
    }
}

void AppManager::EnsureUiTask()
{
    if (task_handle_)
    {
        return;
    }

    BaseType_t res = xTaskCreate(&AppManager::UiTask, "ui_shell", 4096, this, 3, &task_handle_);
    if (res != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create UI task (%d)", res);
        task_handle_ = nullptr;
    }
}

void AppManager::UiTask(void *arg)
{
    auto *self = static_cast<AppManager *>(arg);
    if (self)
    {
        self->UiLoop();
    }
    vTaskDelete(nullptr);
}

void AppManager::UiLoop()
{
    const TickType_t delay_ticks = pdMS_TO_TICKS(100);
    while (true)
    {
        if (current_)
        {
            current_->OnTick(100);
        }
        vTaskDelay(delay_ticks);
    }
}

void AppManager::EnterSelected()
{
    if (apps_.empty())
    {
        return;
    }
    current_ = apps_[selected_index_];
    if (current_)
    {
        current_->OnEnter();
        RenderStatus("Running", current_->title());
    }
}

void AppManager::ExitCurrent()
{
    if (!current_)
    {
        return;
    }
    current_->OnExit();
    current_.reset();
    RenderHome();
}

void AppManager::RenderHome()
{
    if (apps_.empty())
    {
        return;
    }

    if (EpdRenderer::Available())
    {
        EpdRenderer::Clear();
        EpdRenderer::DrawText("Apps", 8, 20, EpdRenderer::FontSize::k16);
        int y = 44;
        for (size_t i = 0; i < apps_.size(); ++i)
        {
            std::string line = (static_cast<int>(i) == selected_index_) ? "> " : "  ";
            line += apps_[i]->title();
            EpdRenderer::DrawText(line.c_str(), 10, y, EpdRenderer::FontSize::k16);
            y += 22;
        }
        EpdRenderer::Display(false);
    }
    else
    {
        auto display = Board::GetInstance().GetDisplay();
        std::string buf = "Apps:\n";
        for (size_t i = 0; i < apps_.size(); ++i)
        {
            buf += (static_cast<int>(i) == selected_index_) ? "> " : "  ";
            buf += apps_[i]->title();
            if (i + 1 < apps_.size())
                buf += "\n";
        }
        display->SetChatMessage("system", buf.c_str());
    }
}

void AppManager::RenderStatus(const std::string &headline, const std::string &detail)
{
    if (EpdRenderer::Available())
    {
        EpdRenderer::Clear();
        EpdRenderer::DrawText(headline.c_str(), 8, 24, EpdRenderer::FontSize::k16);
        EpdRenderer::DrawText(detail.c_str(), 8, 48, EpdRenderer::FontSize::k16);
        EpdRenderer::Display(false);
    }
    else
    {
        auto display = Board::GetInstance().GetDisplay();
        std::string msg = headline + "\n" + detail;
        display->SetChatMessage("system", msg.c_str());
    }
}
