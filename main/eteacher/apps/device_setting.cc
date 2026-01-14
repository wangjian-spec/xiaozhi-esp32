#include "eteacher/apps/device_setting.h"

#include "display.h"
#include "ota.h"
#include "settings.h"
#include "assets/lang_config.h"

#include "eteacher/epd_manager/epd_manager.h"
#include "boards/EnglishTeacher/custom_epd_display.h"

#include <esp_log.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <qrcode.h>
#include <wifi_manager.h>

#include <algorithm>
#include <string>
#include <vector>

static const char *TAG = "DeviceSettingApp";

namespace {

static constexpr const char *kTitle = "Settings";

struct MenuItem {
    const char *key;
    const char *title;
};

static constexpr MenuItem kItems[] = {
    {"wifi", "WiFi 配置"},
    {"language", "语言设置"},
    {"ota", "OTA"},
};

static constexpr int kItemCount = static_cast<int>(sizeof(kItems) / sizeof(kItems[0]));

struct MenuDrawCtx {
    CustomEpdDisplay *epd;
    int selected_index;
    std::vector<std::string> rows;
};

void DrawMenuCb(Adafruit_GFX &gfx, void *ctx)
{
    auto *m = static_cast<MenuDrawCtx *>(ctx);
    if (!m || !m->epd)
    {
        return;
    }

    (void)gfx;

    const int16_t x = 8;
    int16_t baseline_y = 20;
    m->epd->DrawUtf8(x, baseline_y, kTitle, "wenquanyi_11pt", GxEPD_BLACK);
    baseline_y += 22;

    for (size_t i = 0; i < m->rows.size(); ++i)
    {
        if (baseline_y > m->epd->height() - 16)
        {
            break;
        }
        std::string row = (static_cast<int>(i) == m->selected_index) ? "> " : "  ";
        row += m->rows[i];
        m->epd->DrawUtf8(x, baseline_y, row, "wenquanyi_11pt", GxEPD_BLACK);
        baseline_y += 20;
    }
}

void DeleteMenuCtx(void *ctx)
{
    delete static_cast<MenuDrawCtx *>(ctx);
}

struct QrDrawCtx {
    Adafruit_GFX *gfx;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

static QrDrawCtx *g_qr_draw_ctx = nullptr;

class QrDrawCtxGuard {
public:
    explicit QrDrawCtxGuard(QrDrawCtx *ctx) { g_qr_draw_ctx = ctx; }
    ~QrDrawCtxGuard() { g_qr_draw_ctx = nullptr; }
    QrDrawCtxGuard(const QrDrawCtxGuard &) = delete;
    QrDrawCtxGuard &operator=(const QrDrawCtxGuard &) = delete;
};

void DrawQrToGfx(esp_qrcode_handle_t qrcode)
{
    auto *d = g_qr_draw_ctx;
    if (!d || !d->gfx)
    {
        return;
    }

    const int size = esp_qrcode_get_size(qrcode);
    const int border = 2;
    const int total = size + border * 2;

    int scale = std::min(d->w / total, d->h / total);
    if (scale < 1)
    {
        scale = 1;
    }

    const int qr_w = total * scale;
    const int qr_h = total * scale;
    const int start_x = d->x + (d->w - qr_w) / 2;
    const int start_y = d->y + (d->h - qr_h) / 2;

    // White background
    d->gfx->fillRect(start_x, start_y, qr_w, qr_h, GxEPD_WHITE);

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            if (esp_qrcode_get_module(qrcode, x, y))
            {
                const int px = start_x + (x + border) * scale;
                const int py = start_y + (y + border) * scale;
                d->gfx->fillRect(px, py, scale, scale, GxEPD_BLACK);
            }
        }
    }
}

struct WifiQrDrawCtx {
    CustomEpdDisplay *epd;
    std::string ssid;
    std::string url;
    std::string qr_text;
};

void DrawWifiQrCb(Adafruit_GFX &gfx, void *ctx)
{
    auto *w = static_cast<WifiQrDrawCtx *>(ctx);
    if (!w || !w->epd)
    {
        return;
    }

    // Clear
    gfx.fillScreen(GxEPD_WHITE);

    // Title and hints
    const int16_t x = 8;
    int16_t baseline_y = 20;
    w->epd->DrawUtf8(x, baseline_y, "WiFi 配网", "wenquanyi_11pt", GxEPD_BLACK);
    baseline_y += 20;

    std::string line1 = "热点: " + w->ssid;
    w->epd->DrawUtf8(x, baseline_y, line1, "wenquanyi_11pt", GxEPD_BLACK);
    baseline_y += 18;

    std::string line2 = "浏览器: " + w->url;
    w->epd->DrawUtf8(x, baseline_y, line2, "wenquanyi_11pt", GxEPD_BLACK);

    // QR area (right side)
    QrDrawCtx qctx{
        .gfx = &gfx,
        .x = static_cast<int16_t>(w->epd->width() - 210),
        .y = 60,
        .w = 200,
        .h = 200,
    };

    QrDrawCtxGuard guard(&qctx);

    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = &DrawQrToGfx;
    cfg.max_qrcode_version = 10;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;

    esp_err_t err = esp_qrcode_generate(&cfg, w->qr_text.c_str());
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "esp_qrcode_generate failed: %s", esp_err_to_name(err));
        std::string err_line = "QR 生成失败: " + std::to_string(err);
        w->epd->DrawUtf8(x, 290, err_line, "wenquanyi_11pt", GxEPD_BLACK);
    }
    else
    {
        w->epd->DrawUtf8(x, 290, "扫码连接热点后打开上面网址", "wenquanyi_11pt", GxEPD_BLACK);
    }
}

void DeleteWifiQrCtx(void *ctx)
{
    delete static_cast<WifiQrDrawCtx *>(ctx);
}

struct OtaDrawCtx {
    CustomEpdDisplay *epd;
    std::string status;
    std::string current_version;
    bool has_new_version;
    std::string new_version;
    std::string url;
};

void DrawOtaCb(Adafruit_GFX &gfx, void *ctx)
{
    auto *o = static_cast<OtaDrawCtx *>(ctx);
    if (!o || !o->epd)
    {
        return;
    }

    gfx.fillScreen(GxEPD_WHITE);
    o->epd->DrawUtf8(8, 20, "OTA", "wenquanyi_11pt", GxEPD_BLACK);
    o->epd->DrawUtf8(8, 44, o->status.empty() ? "..." : o->status, "wenquanyi_11pt", GxEPD_BLACK);

    int16_t y = 70;
    if (!o->current_version.empty())
    {
        o->epd->DrawUtf8(8, y, std::string("当前: ") + o->current_version, "wenquanyi_11pt", GxEPD_BLACK);
        y += 22;
    }
    if (o->has_new_version)
    {
        o->epd->DrawUtf8(8, y, std::string("新版本: ") + o->new_version, "wenquanyi_11pt", GxEPD_BLACK);
        y += 22;
        o->epd->DrawUtf8(8, y, std::string("URL: ") + o->url, "wenquanyi_11pt", GxEPD_BLACK);
    }

    o->epd->DrawUtf8(8, 290, "Select 返回菜单 / Back 退出 app", "wenquanyi_11pt", GxEPD_BLACK);
}

void DeleteOtaCtx(void *ctx)
{
    delete static_cast<OtaDrawCtx *>(ctx);
}

} // namespace

DeviceSettingApp::DeviceSettingApp() = default;

MenuMeta DeviceSettingApp::GetMenuMeta() const
{
    return MenuMeta{"device_setting", "System Settings", "WiFi / Language / OTA"};
}

void DeviceSettingApp::OnEnter(AppContext &ctx)
{
    selected_index_ = 0;
    view_ = View::kMenu;
    last_wifi_config_mode_ = false;
    ota_ = OtaUiState{};
    Render(ctx);
}

void DeviceSettingApp::OnExit(AppContext &ctx)
{
    (void)ctx;
}

void DeviceSettingApp::OnButton(AppContext &ctx, const ButtonEvent &event)
{
    if (view_ == View::kWifiQr)
    {
        if (event.id == AppButton::Select)
        {
            view_ = View::kMenu;
            Render(ctx);
        }
        return;
    }

    if (view_ == View::kOta)
    {
        if (event.id == AppButton::Select)
        {
            view_ = View::kMenu;
            Render(ctx);
        }
        return;
    }

    if (event.id == AppButton::Up)
    {
        selected_index_ = (selected_index_ - 1 + kItemCount) % kItemCount;
        Render(ctx);
        return;
    }

    if (event.id == AppButton::Down)
    {
        selected_index_ = (selected_index_ + 1) % kItemCount;
        Render(ctx);
        return;
    }

    if (event.id == AppButton::Select)
    {
        switch (selected_index_)
        {
        case 0:
            EnterWifiQr(ctx);
            break;
        case 1:
            CycleLanguage(ctx);
            break;
        case 2:
            EnterOta(ctx);
            break;
        default:
            break;
        }
    }
}

void DeviceSettingApp::OnTick(AppContext &ctx, uint32_t /*delta_ms*/)
{
    if (view_ == View::kWifiQr)
    {
        bool now = WifiManager::GetInstance().IsConfigMode();
        if (now != last_wifi_config_mode_)
        {
            last_wifi_config_mode_ = now;
            RenderWifiQr(ctx);
        }
    }
}

void DeviceSettingApp::Render(AppContext &ctx)
{
    switch (view_)
    {
    case View::kMenu:
        RenderMenu(ctx);
        break;
    case View::kWifiQr:
        RenderWifiQr(ctx);
        break;
    case View::kOta:
        RenderOta(ctx);
        break;
    }
}

void DeviceSettingApp::RenderMenu(AppContext &ctx)
{
    Settings settings("wifi", false);
    std::string lang = settings.GetString("language", "");
    if (lang.empty())
    {
        lang = Lang::CODE;
    }

    std::vector<std::string> rows;
    rows.reserve(kItemCount);
    for (const auto &it : kItems)
    {
        if (std::string(it.key) == "language")
        {
            rows.push_back(std::string(it.title) + " (" + lang + ")");
        }
        else
        {
            rows.push_back(it.title);
        }
    }

    if (auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay()))
    {
        auto *m = new MenuDrawCtx();
        m->epd = epd;
        m->selected_index = selected_index_;
        m->rows = std::move(rows);

        EpdManager::GetInstance().Schedule(
            EpdManager::TaskType::kPartial,
            &DrawMenuCb,
            m,
            &DeleteMenuCtx,
            EpdManager::Rect(0, 0, epd->width(), epd->height()));
        return;
    }

    auto display = ctx.board.GetDisplay();
    std::string msg = "Settings:\n";
    for (size_t i = 0; i < rows.size(); ++i)
    {
        msg += (static_cast<int>(i) == selected_index_) ? "> " : "  ";
        msg += rows[i];
        if (i + 1 < rows.size())
        {
            msg += "\n";
        }
    }
    msg += "\nSelect to enter, Back to exit";
    display->SetChatMessage("system", msg.c_str());
}

void DeviceSettingApp::EnterWifiQr(AppContext &ctx)
{
#if CONFIG_USE_HOTSPOT_WIFI_PROVISIONING
    WifiManager::GetInstance().StartConfigAp();
    last_wifi_config_mode_ = WifiManager::GetInstance().IsConfigMode();
    view_ = View::kWifiQr;
    Render(ctx);
#else
    ctx.board.GetDisplay()->SetChatMessage("system", "当前固件未启用热点配网 (CONFIG_USE_HOTSPOT_WIFI_PROVISIONING)");
#endif
}

void DeviceSettingApp::RenderWifiQr(AppContext &ctx)
{
#if CONFIG_USE_HOTSPOT_WIFI_PROVISIONING
    auto &wifi = WifiManager::GetInstance();
    std::string ssid = wifi.GetApSsid();
    std::string url = wifi.GetApWebUrl();

    // WiFi QR (open hotspot): scanning prompts phone to join the device AP.
    // After joining, most phones will open captive portal automatically; otherwise open the URL manually.
    std::string qr_text = "WIFI:T:nopass;S:" + ssid + ";;";



    if (auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay()))
    {
        auto *w = new WifiQrDrawCtx();
        w->epd = epd;
        w->ssid = std::move(ssid);
        w->url = std::move(url);
        w->qr_text = std::move(qr_text);

        EpdManager::GetInstance().Schedule(
            EpdManager::TaskType::kPartial,
            &DrawWifiQrCb,
            w,
            &DeleteWifiQrCtx,
            EpdManager::Rect(0, 0, epd->width(), epd->height()));
        return;
    }

    std::string msg = "WiFi 配网\n";
    msg += "热点: " + ssid + "\n";
    msg += "浏览器: " + url + "\n";
    msg += "(EPD 不可用，无法显示二维码)";
    ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
#else
    (void)ctx;
#endif
}

void DeviceSettingApp::CycleLanguage(AppContext &ctx)
{
    static const char *kLangs[] = {"zh-CN", "en-US", "ja-JP"};
    static constexpr int kLangCount = static_cast<int>(sizeof(kLangs) / sizeof(kLangs[0]));

    Settings settings_ro("wifi", false);
    std::string cur = settings_ro.GetString("language", "");
    if (cur.empty())
    {
        cur = Lang::CODE;
    }

    int idx = 0;
    for (int i = 0; i < kLangCount; ++i)
    {
        if (cur == kLangs[i])
        {
            idx = i;
            break;
        }
    }

    const int next = (idx + 1) % kLangCount;

    Settings settings("wifi", true);
    settings.SetString("language", kLangs[next]);

    std::string msg = "语言已设置为: ";
    msg += kLangs[next];
    msg += " (重启后生效)";
    ctx.board.GetDisplay()->ShowNotification(msg, 3000);
    RenderMenu(ctx);
}

void DeviceSettingApp::EnterOta(AppContext &ctx)
{
    view_ = View::kOta;
    ota_ = OtaUiState{};
    ota_.status_line = "Checking update...";
    RenderOta(ctx);

    // Run OTA check in background to avoid blocking button task.
    xTaskCreate(
        [](void *arg) {
            auto *app = static_cast<DeviceSettingApp *>(arg);
            if (!app)
            {
                vTaskDelete(nullptr);
                return;
            }

            app->ota_.task_running = true;

            Ota ota;
            esp_err_t err = ota.CheckVersion();
            app->ota_.check_done = true;
            app->ota_.task_running = false;

            if (err != ESP_OK)
            {
                app->ota_.status_line = std::string("Check failed: ") + esp_err_to_name(err);
            }
            else
            {
                app->ota_.current_version = ota.GetCurrentVersion();
                app->ota_.has_new_version = ota.HasNewVersion();
                if (ota.HasNewVersion())
                {
                    app->ota_.new_version = ota.GetFirmwareVersion();
                    app->ota_.url = ota.GetFirmwareUrl();
                    app->ota_.status_line = "发现新版本 (Select 返回)";
                }
                else
                {
                    app->ota_.status_line = "已是最新版本 (Select 返回)";
                }
            }

            // Refresh OTA screen if still on OTA view (best-effort).
            // Avoid calling app methods that need AppContext; schedule draw directly.
            if (auto *epd = dynamic_cast<CustomEpdDisplay *>(Board::GetInstance().GetDisplay()))
            {
                auto *o = new OtaDrawCtx();
                o->epd = epd;
                o->status = app->ota_.status_line;
                o->current_version = app->ota_.current_version;
                o->has_new_version = app->ota_.has_new_version;
                o->new_version = app->ota_.new_version;
                o->url = app->ota_.url;
                EpdManager::GetInstance().Schedule(
                    EpdManager::TaskType::kPartial,
                    &DrawOtaCb,
                    o,
                    &DeleteOtaCtx,
                    EpdManager::Rect(0, 0, epd->width(), epd->height()));
            }
            else
            {
                Board::GetInstance().GetDisplay()->SetChatMessage("system", app->ota_.status_line.c_str());
            }

            vTaskDelete(nullptr);
        },
        "ota_check",
        8192,
        this,
        2,
        nullptr);
}

void DeviceSettingApp::RenderOta(AppContext &ctx)
{
    if (auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay()))
    {
        auto *o = new OtaDrawCtx();
        o->epd = epd;
        o->status = ota_.status_line;
        o->current_version = ota_.current_version;
        o->has_new_version = ota_.has_new_version;
        o->new_version = ota_.new_version;
        o->url = ota_.url;

        EpdManager::GetInstance().Schedule(
            EpdManager::TaskType::kPartial,
            &DrawOtaCb,
            o,
            &DeleteOtaCtx,
            EpdManager::Rect(0, 0, epd->width(), epd->height()));
        return;
    }

    std::string msg = "OTA\n";
    msg += ota_.status_line.empty() ? "..." : ota_.status_line;
    if (!ota_.current_version.empty())
    {
        msg += "\n当前: " + ota_.current_version;
    }
    if (ota_.has_new_version)
    {
        msg += "\n新版本: " + ota_.new_version;
        msg += "\nURL: " + ota_.url;
    }
    ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
}

std::unique_ptr<AppBase> MakeDeviceSettingApp()
{
    return std::make_unique<DeviceSettingApp>();
}
