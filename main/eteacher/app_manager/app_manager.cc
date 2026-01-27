#include "display.h"
#include "eteacher/app_manager/app_manager.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "audio/audio_codec.h"
#include <esp_log.h>
#include <wifi_manager.h>

#include <string>
#include <vector>
#include <ctime>
#include <cstdio>

namespace {

struct MenuDrawCtx {
    CustomEpdDisplay *epd;
    const eteacher::app_menu::Menu *menu;
    int selected_index;
    std::vector<eteacher::app_menu::MenuItem> items;
    eteacher::app_menu::MenuStatus status;
    std::string footer_text;
};

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

void DeleteMenuCtx(void *ctx)
{
    delete static_cast<MenuDrawCtx *>(ctx);
}

std::string FormatTimeText()
{
    std::time_t now = std::time(nullptr);
    if (now <= 0)
    {
        return "----年--月--日 星期-  --:--";
    }
    std::tm local_tm{};
    localtime_r(&now, &local_tm);
    static const char *kWeekday[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
    const char *weekday = "星期-";
    if (local_tm.tm_wday >= 0 && local_tm.tm_wday < 7)
    {
        weekday = kWeekday[local_tm.tm_wday];
    }
    char buf[64] = {0};
    std::snprintf(buf,
                  sizeof(buf),
                  "%04d年%02d月%02d日 %s  %02d:%02d",
                  local_tm.tm_year + 1900,
                  local_tm.tm_mon + 1,
                  local_tm.tm_mday,
                  weekday,
                  local_tm.tm_hour,
                  local_tm.tm_min);
    return std::string(buf);
}

int GetCurrentMinuteOfDay()
{
    std::time_t now = std::time(nullptr);
    if (now <= 0)
    {
        return -1;
    }
    std::tm local_tm{};
    localtime_r(&now, &local_tm);
    return local_tm.tm_hour * 60 + local_tm.tm_min;
}

std::string FormatBatteryText(Board &board)
{
    int level = 0;
    bool charging = false;
    bool discharging = false;
    if (!board.GetBatteryLevel(level, charging, discharging))
    {
        return "--";
    }
    std::string text = std::to_string(level) + "%";
    if (charging)
    {
        text += "+";
    }
    return text;
}

int GetBatteryLevelPercent(Board &board)
{
    int level = 0;
    bool charging = false;
    bool discharging = false;
    if (!board.GetBatteryLevel(level, charging, discharging))
    {
        return 50;
    }
    if (level < 0)
    {
        return 0;
    }
    if (level > 100)
    {
        return 100;
    }
    return level;
}

std::string FormatVolumeText(Board &board)
{
    auto *codec = board.GetAudioCodec();
    if (!codec)
    {
        return "--";
    }
    return std::to_string(codec->output_volume()) + "%";
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

    bool moved = false;
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
            moved = true;
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

void AppManager::Tick(uint32_t delta_ms)
{
    //更新上栏和下栏内容，检测间隔为1秒，当有时间，电量，WIFI变化时，刷新上栏和下栏（目前为整个菜单）
    //此处代码需要检查，需要修改，确认是否重复刷新了
    if (running_ && ctx_)
    {
        running_->OnTick(*ctx_, delta_ms);
        return;
    }
    if (!ctx_ || !menu_ready_)
    {
        return;
    }

    menu_tick_accum_ += delta_ms;
    if (menu_tick_accum_ < 1000)
    {
        return;
    }
    menu_tick_accum_ = 0;

    const int minute_now = GetCurrentMinuteOfDay();
    if (minute_now >= 0 && minute_now != last_time_minute_)
    {
        RenderMenu();
        return;
    }

    bool need_refresh = false;
    const bool wifi_connected = WifiManager::GetInstance().IsConnected();
    if (wifi_connected != last_wifi_connected_)
    {
        need_refresh = true;
    }
    const int battery_level = GetBatteryLevelPercent(ctx_->board);
    if (battery_level != last_battery_level_)
    {
        need_refresh = true;
    }
    if (need_refresh)
    {
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

    // Prefer native EPD render when available
    if (auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx_->board.GetDisplay()))
    {
        // Render menu via EpdManager partial refresh:
        // - non-blocking (runs in EpdManager task)
        // - uses `displayWindow(...)` internally
        // - text uses EPD UTF-8 API with WenQuanYi built-in fonts
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

        last_layout_ = menu_.ComputeLayout(epd->width(), epd->height(), items.size());
        menu_controller_.SetLayout(last_layout_, static_cast<int>(items.size()));
        menu_controller_.SetSelected(selected_index_);

        eteacher::app_menu::MenuStatus status;
        status.time_text = FormatTimeText();
        status.wifi_text.clear();
        status.wifi_connected = WifiManager::GetInstance().IsConnected();
        status.battery_text = FormatBatteryText(ctx_->board);
        status.battery_level = GetBatteryLevelPercent(ctx_->board);
        status.volume_text = FormatVolumeText(ctx_->board);

        last_time_minute_ = GetCurrentMinuteOfDay();
        last_wifi_connected_ = status.wifi_connected;
        last_battery_level_ = status.battery_level;

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
        struct StatusDrawCtx {
            CustomEpdDisplay *epd;
            std::string headline;
            std::string detail;
        };

        auto *s = new StatusDrawCtx();
        s->epd = epd;
        s->headline = headline;
        s->detail = detail;

        auto draw = [](Adafruit_GFX &gfx, void *ctx) {
            auto *st = static_cast<StatusDrawCtx *>(ctx);
            if (!st || !st->epd)
            {
                return;
            }
            gfx.fillScreen(GxEPD_WHITE);
            st->epd->DrawUtf8(8, 24, st->headline, "wenquanyi_11pt", GxEPD_BLACK);
            st->epd->DrawUtf8(8, 48, st->detail, "wenquanyi_11pt", GxEPD_BLACK);
        };

        auto del = [](void *ctx) { delete static_cast<StatusDrawCtx *>(ctx); };

        EpdManager::GetInstance().Schedule(
            EpdManager::TaskType::kPartial,
            draw,
            s,
            del,
            EpdManager::Rect(0, 0, epd->width(), epd->height()));
        return;
    }

    auto display = ctx_->board.GetDisplay();
    std::string msg = headline + "\n" + detail;
    display->SetChatMessage("system", msg.c_str());
}
