#include "display.h"
#include "eteacher/app_manager/app_manager.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"
#include <esp_log.h>

#include <string>
#include <vector>

namespace {

struct MenuDrawCtx {
    CustomEpdDisplay *epd;
    int selected_index;
    std::vector<MenuMeta> items;
};

void DrawMenuCb(Adafruit_GFX &gfx, void *ctx)
{
    auto *m = static_cast<MenuDrawCtx *>(ctx);
    if (!m || !m->epd)
    {
        return;
    }
    // Window clear is handled by EpdManager before invoking this callback.
    // Keep the callback focused on drawing only.

    const int16_t x = 8;
    int16_t baseline_y = 20;
    m->epd->DrawUtf8(x, baseline_y, "Apps", "wenquanyi_11pt", GxEPD_BLACK);
    baseline_y += 20;

    for (size_t i = 0; i < m->items.size(); ++i)
    {
        if (baseline_y > m->epd->height() - 16)
        {
            break;
        }

        std::string row_text;
        row_text.reserve(2 + m->items[i].title.size() + 3 + m->items[i].subtitle.size());
        row_text += (static_cast<int>(i) == m->selected_index) ? "> " : "  ";
        row_text += m->items[i].title;
        if (!m->items[i].subtitle.empty())
        {
            row_text += " - ";
            row_text += m->items[i].subtitle;
        }
        m->epd->DrawUtf8(x, baseline_y, row_text, "wenquanyi_11pt", GxEPD_BLACK);
        baseline_y += 18;
    }
}

void DeleteMenuCtx(void *ctx)
{
    delete static_cast<MenuDrawCtx *>(ctx);
}

} // namespace

static const char* TAG = "AppManager";

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
    EnsureSelectionValid();
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
    case AppButton::Back:
        RenderMenu();
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
    ESP_LOGI(TAG, "Menu select %d/%d", selected_index_, size);
    RenderMenu();
}

void AppManager::EnsureSelectionValid()
{
    if (apps_.empty())
    {
        selected_index_ = 0;
        return;
    }
    if (selected_index_ < 0)
    {
        selected_index_ = 0;
    }
    if (selected_index_ >= static_cast<int>(apps_.size()))
    {
        selected_index_ = static_cast<int>(apps_.size()) - 1;
    }
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
        // Render menu via EpdManager partial refresh:
        // - non-blocking (runs in EpdManager task)
        // - uses `displayWindow(...)` internally
        // - text uses EPD UTF-8 API with WenQuanYi built-in fonts
        auto *m = new MenuDrawCtx();
        m->epd = epd;
        m->selected_index = selected_index_;
        m->items.reserve(apps_.size());
        for (const auto &app : apps_)
        {
            m->items.push_back(app->GetMenuMeta());
        }

        EpdManager::GetInstance().Schedule(
            EpdManager::TaskType::kPartial,
            &DrawMenuCb,
            m,
            &DeleteMenuCtx,
            EpdManager::Rect(0, 0, epd->width(), epd->height()));
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
        auto &gfx = epd->Driver();
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
