#include "display.h"
#include "eteacher/app_manager/app_manager.h"
#include "boards/EnglishTeacher/custom_epd_display.h"

AppManager &AppManager::GetInstance()
{
    static AppManager inst;
    return inst;
}

void AppManager::Init(Board &board)
{
    static AppContext ctx(board);
    ctx_ = &ctx;
    ShowMenu();
}

void AppManager::Register(std::unique_ptr<AppBase> app)
{
    if (!app || !app->show_in_menu())
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
    RenderMenu();
}

void AppManager::ShowMenu()
{
    running_ = nullptr;
    RenderMenu();
}

void AppManager::HandleButton(const ButtonEvent &event)
{
    if (!ctx_ || apps_.empty())
    {
        return;
    }

    if (running_)
    {
        if (event.id == AppButton::Back)
        {
            ExitCurrent();
            return;
        }
        running_->OnButton(*ctx_, event);
        return;
    }

    switch (event.id)
    {
    case AppButton::Up:
        MoveSelection(-1);
        break;
    case AppButton::Down:
        MoveSelection(1);
        break;
    case AppButton::Select:
        EnterCurrent();
        break;
    default:
        break;
    }
}

void AppManager::Tick(uint32_t delta_ms)
{
    if (running_ && ctx_)
    {
        running_->OnTick(*ctx_, delta_ms);
    }
}

void AppManager::EnterCurrent()
{
    if (!ctx_ || apps_.empty())
    {
        return;
    }
    running_ = apps_[selected_index_].get();
    running_->OnEnter(*ctx_);
    RenderStatus("Running", running_->GetMenuMeta().title);
}

void AppManager::ExitCurrent()
{
    if (!running_ || !ctx_)
    {
        return;
    }
    running_->OnExit(*ctx_);
    running_ = nullptr;
    RenderMenu();
}

void AppManager::MoveSelection(int step)
{
    if (apps_.empty())
    {
        return;
    }
    const int size = static_cast<int>(apps_.size());
    selected_index_ = (selected_index_ + step + size) % size;
    RenderMenu();
}

void AppManager::RenderMenu()
{
    if (!ctx_ || apps_.empty())
    {
        return;
    }

    // Prefer native EPD render when available
    if (auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx_->board.GetDisplay()))
    {
        DisplayLockGuard guard(epd);
        auto &gfx = epd->Epd();
        gfx.setFullWindow();
        gfx.firstPage();
        do
        {
            gfx.fillScreen(GxEPD_WHITE);
            gfx.setTextColor(GxEPD_BLACK);
            gfx.setFont(nullptr);
            gfx.setTextSize(1);

            int16_t x = 8;
            int16_t y = 20;
            gfx.setCursor(x, y);
            gfx.print("Apps");
            y += 20;

            for (size_t i = 0; i < apps_.size(); ++i)
            {
                gfx.setCursor(x, y);
                if (static_cast<int>(i) == selected_index_)
                {
                    gfx.print("> ");
                }
                else
                {
                    gfx.print("  ");
                }
                gfx.print(apps_[i]->GetMenuMeta().title.c_str());
                if (!apps_[i]->GetMenuMeta().subtitle.empty())
                {
                    gfx.print(" - ");
                    gfx.print(apps_[i]->GetMenuMeta().subtitle.c_str());
                }
                y += 18;
                if (y > epd->height() - 16)
                {
                    break;
                }
            }
        } while (gfx.nextPage());
        return;
    }

    auto display = ctx_->board.GetDisplay();
    std::string buf = "Apps:\n";
    for (size_t i = 0; i < apps_.size(); ++i)
    {
        buf += (static_cast<int>(i) == selected_index_) ? "> " : "  ";
        buf += apps_[i]->GetMenuMeta().title;
        if (!apps_[i]->GetMenuMeta().subtitle.empty())
        {
            buf += " - " + apps_[i]->GetMenuMeta().subtitle;
        }
        if (i + 1 < apps_.size())
            buf += "\n";
    }
    display->SetChatMessage("system", buf.c_str());
}

void AppManager::RenderStatus(const std::string &headline, const std::string &detail)
{
    if (!ctx_)
    {
        return;
    }
    if (auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx_->board.GetDisplay()))
    {
        DisplayLockGuard guard(epd);
        auto &gfx = epd->Epd();
        gfx.setFullWindow();
        gfx.firstPage();
        do
        {
            gfx.fillScreen(GxEPD_WHITE);
            gfx.setTextColor(GxEPD_BLACK);
            gfx.setFont(nullptr);
            gfx.setTextSize(1);
            gfx.setCursor(8, 24);
            gfx.print(headline.c_str());
            gfx.setCursor(8, 44);
            gfx.print(detail.c_str());
        } while (gfx.nextPage());
        return;
    }

    auto display = ctx_->board.GetDisplay();
    std::string msg = headline + "\n" + detail;
    display->SetChatMessage("system", msg.c_str());
}
