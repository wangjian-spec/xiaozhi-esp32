#include "display.h"
#include "eteacher/app_manager/app_manager.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "eteacher/app_ui/status_bar.h"
#include "audio/audio_codec.h"
#include <esp_log.h>
#include <wifi_manager.h>

#include <string>
#include <vector>
#include <ctime>

namespace {
// Context structure for menu drawing callback.
struct MenuDrawCtx {
    CustomEpdDisplay *epd;
    const eteacher::app_menu::Menu *menu;
    int selected_index;
    std::vector<eteacher::app_menu::MenuItem> items;
    eteacher::app_menu::MenuStatus status;
    std::string footer_text;
};
// Menu drawing callback function.
void DrawMenuCb(Adafruit_GFX &gfx, void *ctx)
{
    auto *m = static_cast<MenuDrawCtx *>(ctx);
    if (!m || !m->epd || !m->menu)
    {
        return;
    }
    // Window clear is handled by EpdManager before invoking this callback.
    // Keep the callback focused on drawing only.
    m->menu->Draw(gfx, m->epd, m->items, m->selected_index, m->status, m->footer_text);
}
// Menu drawing context cleanup function.
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
    running_ = nullptr;
    menu_ready_ = false;

    menu_.SetStyle(eteacher::app_menu::MenuStyle{});
}

void AppManager::Register(std::unique_ptr<AppBase> app)
{
    if (!app || !app->show_in_menu())
    {
        return;
    }
    if (app->icon().empty())
    {
        const auto meta = app->GetMenuMeta();
        if (!meta.key.empty())
        {
            app->SetIcon(meta.key + ".bin");
        }
    }
    apps_.push_back(std::move(app));
    EnsureSelectionValid();
}

void AppManager::FinalizeRegistration()
{
    menu_ready_ = true;
    EnsureSelectionValid();
    RenderMenu();
}

void AppManager::ShowMenu()
{
    running_ = nullptr;
    if (menu_ready_)
    {
        RenderMenu();
    }
}

void AppManager::SetMenuFooterText(std::string text)
{
    menu_footer_text_ = std::move(text);
}

void AppManager::HandleButton(const ButtonEvent &event)
{
// 如果没有上下文或没有 App，直接返回
    if (!ctx_ || apps_.empty())
    {
        return;
    }

// 如果有正在运行的 App，优先将按钮事件传递给它，如果按下 Select 键则退出当前 App
    if (running_)
    {
        if (event.id == AppButton::Select)
        {
            ExitCurrent();
            return;
        }
        running_->OnButton(*ctx_, event);
        return;
    }

// 如果没有正在运行的 App，则处理菜单导航
    switch (event.id)
    {
    case AppButton::Up:
    case AppButton::Down:
    case AppButton::Left:
    case AppButton::Right:
        if (menu_controller_.Move(event.id))
        {
            selected_index_ = menu_controller_.selected();
            ESP_LOGI(TAG, "Menu select %d/%d", selected_index_, static_cast<int>(apps_.size()));
            RenderMenu();
        }
        break;
    case AppButton::Start:
        EnterCurrent();
        break;
    case AppButton::Select:
    case AppButton::A:
    case AppButton::B:
    case AppButton::C:
    case AppButton::D:
    default:
        break;
    }
}

void AppManager::RefreshMenu()
{
    if (menu_ready_ && !running_)
    {
        RenderMenu();
    }
}

void AppManager::Tick()
{
    //如果没有上下文或菜单未准备好，直接返回
    if (!ctx_ || !menu_ready_)
    {
        return;
    }

    // Fixed 1s tick — check menu status every call.
    bool status_changed = eteacher::app_ui::CheckAndUpdateMenuStatus(ctx_->board, last_status_);
    ESP_LOGI(TAG, "CheckAndUpdateMenuStatus -> %s", status_changed ? "true" : "false");
    // 如果状态有变化且没有正在运行的 App，则重新渲染菜单
    if (status_changed && !running_) {
        RenderMenu();
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
    if (!ctx_ || apps_.empty() || !menu_ready_)
    {
        return;
    }
    // Build menu items once and reuse for both EPD and fallback paths.
    std::vector<eteacher::app_menu::MenuItem> items;
    items.reserve(apps_.size());
    for (const auto &app : apps_)
    {
        auto meta = app->GetMenuMeta();
        std::string icon = app->icon();
        if (icon.empty() && !meta.key.empty())
        {
            icon = meta.key + ".bin";
        }
        items.push_back({meta, std::move(icon)});
    }

    // Prefer native EPD render when available
    if (auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx_->board.GetDisplay()))
    {
        // Render menu via EpdManager partial refresh (non-blocking task).
        last_layout_ = menu_.ComputeLayout(epd->width(), epd->height(), items.size());
        menu_controller_.SetLayout(last_layout_, static_cast<int>(items.size()));
        menu_controller_.SetSelected(selected_index_);

        eteacher::app_menu::MenuStatus status = eteacher::app_ui::BuildMenuStatus(ctx_->board);

        auto *m = new MenuDrawCtx();
        m->epd = epd;
        m->menu = &menu_;
        m->selected_index = selected_index_;
        m->items = std::move(items);
        m->status = std::move(status);
        m->footer_text = menu_footer_text_;

        EpdManager::GetInstance().Schedule(
            EpdManager::TaskType::kPartial,
            &DrawMenuCb,
            m,
            &DeleteMenuCtx,
            EpdManager::Rect(0, 0, epd->width(), epd->height()));
        return;
    }

    // Fallback: plain text display
    auto display = ctx_->board.GetDisplay();
    std::string buf = "Apps:\n";
    for (size_t i = 0; i < items.size(); ++i)
    {
        buf += (static_cast<int>(i) == selected_index_) ? "> " : "  ";
        buf += items[i].meta.title;
        if (!items[i].meta.subtitle.empty())
        {
            buf += " - " + items[i].meta.subtitle;
        }
        if (i + 1 < items.size())
            buf += "\n";
    }
    display->SetChatMessage("system", buf.c_str());
}

void AppManager::TickAppRunning()
{
    if (running_ && ctx_)
    {
        running_->OnTick(*ctx_);
    }
}


