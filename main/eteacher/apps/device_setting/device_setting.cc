#include "eteacher/apps/device_setting/device_setting.h"

#include <Adafruit_GFX.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_manager/menu.h"
#include "eteacher/app_ui/layout_engine.h"
#include "eteacher/app_ui/soft_keyboard.h"
#include "eteacher/app_ui/status_bar.h"
#include "eteacher/app_ui/ui_widget.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "system_info.h"
#include <ssid_manager.h>
#include <wifi_manager.h>

namespace {

static const char* TAG = "DeviceSetting";

constexpr int16_t kTopBarH = 20;
constexpr int16_t kBottomBarH = 16;
constexpr int16_t kPadding = 4;
constexpr int16_t kMenuBarH = 20;
constexpr int16_t kGap = 2;

struct UiLayout {
    eteacher::app_ui::layout::Rect header;
    eteacher::app_ui::layout::Rect footer;
    eteacher::app_ui::layout::Rect content;
    eteacher::app_ui::layout::Rect menu;
    eteacher::app_ui::layout::Rect body;
    eteacher::app_ui::layout::Rect body_left;
    eteacher::app_ui::layout::Rect body_right;
    eteacher::app_ui::layout::Rect keyboard;
};

UiLayout ComputeLayout(CustomEpdDisplay* epd) {
    UiLayout out{};
    if (!epd) return out;
    const int16_t w = static_cast<int16_t>(epd->width());
    const int16_t h = static_cast<int16_t>(epd->height());
    out.header = {0, 0, w, kTopBarH};
    out.footer = {0, static_cast<int16_t>(h - kBottomBarH), w, kBottomBarH};
    out.content = {0, kTopBarH, w, static_cast<int16_t>(h - kTopBarH - kBottomBarH)};
    out.menu = {static_cast<int16_t>(out.content.x + kPadding),
                static_cast<int16_t>(out.content.y + kPadding),
                static_cast<int16_t>(out.content.w - kPadding * 2),
                kMenuBarH};
    out.body = {static_cast<int16_t>(out.content.x + kPadding),
                static_cast<int16_t>(out.menu.y + out.menu.h + kGap),
                static_cast<int16_t>(out.content.w - kPadding * 2),
                static_cast<int16_t>(out.content.h - (out.menu.h + kGap) - kPadding)};

    // Split body into left/right for WiFi pages
    const int16_t left_w = static_cast<int16_t>((out.body.w * 65) / 100);
    out.body_left = {out.body.x, out.body.y, left_w, out.body.h};
    out.body_right = {static_cast<int16_t>(out.body.x + left_w + kGap),
                      out.body.y,
                      static_cast<int16_t>(out.body.w - left_w - kGap),
                      out.body.h};

    // Keyboard occupies bottom half of left area
    const int16_t kb_h = static_cast<int16_t>((out.body_left.h * 55) / 100);
    out.keyboard = {out.body_left.x,
                    static_cast<int16_t>(out.body_left.y + out.body_left.h - kb_h),
                    out.body_left.w,
                    kb_h};

    return out;
}

std::string MaskPassword(std::string_view pwd) {
    return std::string(pwd.size(), '*');
}

} // namespace

struct DeviceSettingApp::Impl {
public:
    enum class ViewMode {
        MainMenu,
        WifiSub,
        WifiAdd,
        WifiSaved,
        Language,
        User,
        Learning,
        DeviceInfo,
    };

    enum class WifiAddFocus {
        ScanButton,
        ScanList,
        Keyboard,
    };

    void OnEnter(AppContext &ctx) {
        ctx_ = &ctx;
        mode_ = ViewMode::MainMenu;
        main_index_ = 0;
        wifi_sub_index_ = 0;
        wifi_add_focus_ = WifiAddFocus::ScanButton;
        scan_results_.clear();
        saved_results_.clear();
        saved_selected_ = 0;
        password_.clear();
        status_msg_.clear();
        connect_msg_.clear();
        connecting_ = false;
        last_tick_ms_ = 0;
        keyboard_.SetOnInput([this](std::string_view s) {
            password_.append(s.data(), s.size());
        });
        keyboard_.SetOnDelete([this]() {
            if (!password_.empty()) {
                password_.pop_back();
            }
        });
        Render(ctx);
    }

    void OnExit(AppContext &ctx) {
        (void)ctx;
        ctx_ = nullptr;
    }

    void OnButton(AppContext &ctx, const ButtonEvent &event) {
        if (event.action != ButtonAction::Click) {
            return;
        }

        switch (mode_) {
        case ViewMode::MainMenu:
            HandleMainMenu(ctx, event);
            break;
        case ViewMode::WifiSub:
            HandleWifiSub(ctx, event);
            break;
        case ViewMode::WifiAdd:
            HandleWifiAdd(ctx, event);
            break;
        case ViewMode::WifiSaved:
            HandleWifiSaved(ctx, event);
            break;
        case ViewMode::Language:
        case ViewMode::User:
        case ViewMode::Learning:
        case ViewMode::DeviceInfo:
            HandleSimplePage(ctx, event);
            break;
        }
    }

    void OnTick(AppContext &ctx) {
        // AppManager provides fixed 1s ticks; update accumulators accordingly.
        last_tick_ms_ += 1000;
        if (last_tick_ms_ < 500) {
            return;
        }
        last_tick_ms_ = 0;

        bool wifi_render_needed = false;
        if (mode_ == ViewMode::WifiAdd) {
            auto &wifi = WifiManager::GetInstance();
            if (wifi.IsConnected()) {
                connect_msg_ = "已连接: " + wifi.GetSsid();
                connecting_ = false;
            } else if (connecting_) {
                const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
                if (now - connect_start_ms_ > 5000) {
                    connecting_ = false;
                    status_msg_ = "连接失败";
                }
            }
            wifi_render_needed = true;
        }

        // Periodically check top-bar status (time/wifi/battery/volume). If changed,
        // update cached status and re-render this app to reflect new values.
        bool status_changed = eteacher::app_ui::CheckAndUpdateMenuStatus(ctx.board, last_status_);
        if (status_changed) {
            Render(ctx);
            return;
        }

        if (wifi_render_needed) {
            Render(ctx);
        }
    }

    void Render(AppContext &ctx) {
        auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay());
        if (!epd) {
            ctx.board.GetDisplay()->SetChatMessage("system", "DeviceSetting: EPD unavailable");
            return;
        }

        const UiLayout layout = ComputeLayout(epd);
        if (mode_ == ViewMode::WifiAdd) {
            keyboard_.ShowKeyboard(epd, layout.keyboard.x, layout.keyboard.y, layout.keyboard.w, layout.keyboard.h);
        } else {
            keyboard_.CloseKeyboard();
        }

        struct DrawCtx {
            CustomEpdDisplay *epd;
            AppContext *ctx;
            ViewMode mode;
            int main_index;
            int wifi_sub_index;
            WifiAddFocus wifi_add_focus;
            std::vector<std::string> scan_results;
            std::vector<std::string> saved_results;
            int scan_selected;
            int saved_selected;
            std::string password;
            std::string status_msg;
            std::string connect_msg;
            std::string device_mac;
            std::string device_model;
            std::string firmware;
            std::string dict_ver;
            bool keyboard_visible;
            eteacher::app_ui::KeyboardWidget keyboard;
        };

        auto *draw_ctx = new DrawCtx{
            epd,
            &ctx,
            mode_,
            main_index_,
            wifi_sub_index_,
            wifi_add_focus_,
            scan_results_,
            saved_results_,
            scan_selected_,
            saved_selected_,
            password_,
            status_msg_,
            connect_msg_,
            SystemInfo::GetMacAddress(),
            SystemInfo::GetChipModelName(),
            "v1.0.0",
            "v1.0",
            mode_ == ViewMode::WifiAdd,
            keyboard_};

        auto cb = [](Adafruit_GFX &gfx, void *ctx_ptr) {
            auto *d = static_cast<DrawCtx *>(ctx_ptr);
            if (!d || !d->epd || !d->ctx) return;

            const UiLayout layout = ComputeLayout(d->epd);

            gfx.fillScreen(GxEPD_WHITE);

            // Top/Bottom bar
            eteacher::app_menu::MenuStyle style;
            eteacher::app_menu::MenuStatus status = eteacher::app_ui::BuildMenuStatus(d->ctx->board);
            eteacher::app_ui::DrawTopBar(gfx, d->epd, style, status);

            std::string footer = "C 进入  B 返回";
            if (d->mode == ViewMode::WifiAdd) {
                footer = "C 连接  D 输入  B 返回";
            }
            eteacher::app_ui::DrawBottomBar(gfx, d->epd, style, footer);

            // Menu bar (TabViewWidget)
            std::vector<std::string> menu_items = {"WIFI设置", "语言设置", "用户设置", "学习偏好", "设备信息"};
            eteacher::app_ui::TabViewWidget menu_tabs;
            menu_tabs.SetFont("wenquanyi_9pt");
            menu_tabs.SetPadding(2);
            const int16_t tab_h = static_cast<int16_t>(eteacher::app_ui::GetFontHeight("wenquanyi_9pt") + 4);
            const int16_t menu_h = std::min(layout.menu.h, tab_h);
            eteacher::app_ui::layout::Region menu_region{{eteacher::app_ui::layout::RegionType::Header, 0},
                                                         {layout.menu.x, layout.menu.y, layout.menu.w, menu_h}};
            menu_tabs.AttachRegion(&menu_region);
            menu_tabs.SetTabs(&menu_items);
            menu_tabs.SetSelected(d->main_index);
            menu_tabs.Draw(gfx, d->epd);

            // Content
            if (d->mode == ViewMode::MainMenu) {
                if (d->main_index == 0) {
                    std::vector<std::string> sub = {"添加网络", "已保存网络"};
                    eteacher::app_ui::ListWidget list;
                    list.SetFont("wenquanyi_11pt");
                    eteacher::app_ui::layout::Region list_region{{eteacher::app_ui::layout::RegionType::List, 0}, layout.body};
                    list.AttachRegion(&list_region);
                    list.SetItems(&sub);
                    list.SetSelected(d->wifi_sub_index);
                    list.Draw(gfx, d->epd);
                } else {
                    eteacher::app_ui::LabelWidget hint;
                    hint.SetFont("wenquanyi_11pt");
                    hint.SetAlign(eteacher::app_ui::TextAlign::Center);
                    hint.SetText("按 C 进入设置");
                    eteacher::app_ui::layout::Region hint_region{{eteacher::app_ui::layout::RegionType::Primary, 0}, layout.body};
                    hint.AttachRegion(&hint_region);
                    hint.Draw(gfx, d->epd);
                }
                return;
            }

            if (d->mode == ViewMode::WifiSub) {
                std::vector<std::string> sub = {"添加网络", "已保存网络"};
                eteacher::app_ui::ListWidget list;
                list.SetFont("wenquanyi_11pt");
                eteacher::app_ui::layout::Region list_region{{eteacher::app_ui::layout::RegionType::List, 0}, layout.body};
                list.AttachRegion(&list_region);
                list.SetItems(&sub);
                list.SetSelected(d->wifi_sub_index);
                list.Draw(gfx, d->epd);
                return;
            }

            if (d->mode == ViewMode::WifiAdd) {
                // Left pane: scan button
                const int16_t scan_h = 18;
                eteacher::app_ui::layout::Rect scan_rect{layout.body_left.x, layout.body_left.y, layout.body_left.w, scan_h};
                eteacher::app_ui::ButtonWidget scan_btn;
                scan_btn.SetFont("wenquanyi_9pt");
                scan_btn.SetLabel("扫描网络");
                eteacher::app_ui::layout::Region scan_region{{eteacher::app_ui::layout::RegionType::Primary, 0}, scan_rect};
                scan_btn.AttachRegion(&scan_region);
                scan_btn.OnFocus(d->wifi_add_focus == WifiAddFocus::ScanButton);
                scan_btn.Draw(gfx, d->epd);

                // Scan list
                eteacher::app_ui::layout::Rect list_rect{layout.body_left.x,
                                                         static_cast<int16_t>(scan_rect.y + scan_rect.h + 2),
                                                         layout.body_left.w,
                                                         static_cast<int16_t>(layout.body_left.h - scan_rect.h - layout.keyboard.h - 24)};
                if (!d->scan_results.empty()) {
                    eteacher::app_ui::ListWidget scan_list;
                    scan_list.SetFont("wenquanyi_9pt");
                    eteacher::app_ui::layout::Region list_region{{eteacher::app_ui::layout::RegionType::List, 0}, list_rect};
                    scan_list.AttachRegion(&list_region);
                    scan_list.SetItems(&d->scan_results);
                    scan_list.SetSelected(d->scan_selected);
                    scan_list.Draw(gfx, d->epd);
                } else {
                    eteacher::app_ui::LabelWidget empty;
                    empty.SetFont("wenquanyi_9pt");
                    empty.SetText("暂无扫描结果");
                    eteacher::app_ui::layout::Region empty_region{{eteacher::app_ui::layout::RegionType::List, 0}, list_rect};
                    empty.AttachRegion(&empty_region);
                    empty.Draw(gfx, d->epd);
                }

                // Password label + input
                const int16_t pwd_h = 16;
                const int16_t pwd_y = static_cast<int16_t>(list_rect.y + list_rect.h + 2);
                const int16_t pwd_label_w = 28;
                eteacher::app_ui::layout::Rect pwd_label_rect{list_rect.x, pwd_y, pwd_label_w, pwd_h};
                eteacher::app_ui::layout::Rect pwd_input_rect{static_cast<int16_t>(list_rect.x + pwd_label_w + 2),
                                                             pwd_y,
                                                             static_cast<int16_t>(list_rect.w - pwd_label_w - 2),
                                                             pwd_h};

                eteacher::app_ui::LabelWidget pwd_label;
                pwd_label.SetFont("wenquanyi_9pt");
                pwd_label.SetText("密码:");
                eteacher::app_ui::layout::Region pwd_label_region{{eteacher::app_ui::layout::RegionType::Secondary, 0}, pwd_label_rect};
                pwd_label.AttachRegion(&pwd_label_region);
                pwd_label.Draw(gfx, d->epd);

                eteacher::app_ui::TextInputWidget pwd_input;
                pwd_input.SetFont("wenquanyi_9pt");
                pwd_input.SetCursorVisible(d->wifi_add_focus == WifiAddFocus::Keyboard);
                pwd_input.SetText(MaskPassword(d->password));
                eteacher::app_ui::layout::Region pwd_input_region{{eteacher::app_ui::layout::RegionType::Secondary, 1}, pwd_input_rect};
                pwd_input.AttachRegion(&pwd_input_region);
                pwd_input.OnFocus(d->wifi_add_focus == WifiAddFocus::Keyboard);
                pwd_input.Draw(gfx, d->epd);

                // Status line above keyboard
                if (!d->status_msg.empty()) {
                    const int16_t status_h = static_cast<int16_t>(eteacher::app_ui::GetFontHeight("wenquanyi_9pt") + 2);
                    eteacher::app_ui::layout::Rect status_rect{layout.body_left.x,
                                                               static_cast<int16_t>(layout.keyboard.y - status_h),
                                                               layout.body_left.w,
                                                               status_h};
                    eteacher::app_ui::LabelWidget status_label;
                    status_label.SetFont("wenquanyi_9pt");
                    status_label.SetText(d->status_msg);
                    eteacher::app_ui::layout::Region status_region{{eteacher::app_ui::layout::RegionType::Footer, 0}, status_rect};
                    status_label.AttachRegion(&status_region);
                    status_label.Draw(gfx, d->epd);
                }

                // Keyboard
                if (d->keyboard_visible) {
                    d->keyboard.Draw(gfx, d->epd);
                }

                // Right pane: connection info
                std::string conn_text = "连接状态:";
                if (!d->connect_msg.empty()) {
                    conn_text += "\n" + d->connect_msg;
                }
                eteacher::app_ui::LabelWidget conn_label;
                conn_label.SetFont("wenquanyi_9pt");
                conn_label.SetText(conn_text);
                eteacher::app_ui::layout::Region conn_region{{eteacher::app_ui::layout::RegionType::Secondary, 2}, layout.body_right};
                conn_label.AttachRegion(&conn_region);
                conn_label.Draw(gfx, d->epd);
                return;
            }

            if (d->mode == ViewMode::WifiSaved) {
                if (d->saved_results.empty()) {
                    eteacher::app_ui::LabelWidget empty;
                    empty.SetFont("wenquanyi_11pt");
                    empty.SetText("暂无已保存网络");
                    eteacher::app_ui::layout::Region empty_region{{eteacher::app_ui::layout::RegionType::Primary, 0}, layout.body};
                    empty.AttachRegion(&empty_region);
                    empty.Draw(gfx, d->epd);
                } else {
                    eteacher::app_ui::ListWidget list;
                    list.SetFont("wenquanyi_11pt");
                    eteacher::app_ui::layout::Region list_region{{eteacher::app_ui::layout::RegionType::List, 0}, layout.body};
                    list.AttachRegion(&list_region);
                    list.SetItems(&d->saved_results);
                    list.SetSelected(d->saved_selected);
                    list.Draw(gfx, d->epd);
                }
                return;
            }

            if (d->mode == ViewMode::Language) {
                std::vector<std::string> langs = {"中文", "English"};
                eteacher::app_ui::ListWidget list;
                list.SetFont("wenquanyi_11pt");
                eteacher::app_ui::layout::Region list_region{{eteacher::app_ui::layout::RegionType::List, 0}, layout.body};
                list.AttachRegion(&list_region);
                list.SetItems(&langs);
                list.SetSelected(0);
                list.Draw(gfx, d->epd);
                return;
            }

            if (d->mode == ViewMode::User) {
                std::vector<std::string> lines = {
                    "新建用户",
                    "删除用户",
                    "切换用户",
                    "当前用户: 默认",
                    "英语水平: 中级",
                    "认识单词: 1200"
                };
                eteacher::app_ui::ListWidget list;
                list.SetFont("wenquanyi_9pt");
                eteacher::app_ui::layout::Region list_region{{eteacher::app_ui::layout::RegionType::List, 0}, layout.body};
                list.AttachRegion(&list_region);
                list.SetItems(&lines);
                list.SetSelected(0);
                list.Draw(gfx, d->epd);
                return;
            }

            if (d->mode == ViewMode::Learning) {
                std::vector<std::string> lines = {
                    "口音: 英式",
                    "语速: 中速"
                };
                eteacher::app_ui::ListWidget list;
                list.SetFont("wenquanyi_11pt");
                eteacher::app_ui::layout::Region list_region{{eteacher::app_ui::layout::RegionType::List, 0}, layout.body};
                list.AttachRegion(&list_region);
                list.SetItems(&lines);
                list.SetSelected(0);
                list.Draw(gfx, d->epd);
                return;
            }

            if (d->mode == ViewMode::DeviceInfo) {
                std::vector<std::string> lines = {
                    "设备型号: " + d->device_model,
                    "固件版本: " + d->firmware,
                    "词库版本: " + d->dict_ver,
                    "序列号: " + d->device_mac,
                    "MAC 地址: " + d->device_mac
                };
                eteacher::app_ui::ListWidget list;
                list.SetFont("wenquanyi_9pt");
                eteacher::app_ui::layout::Region list_region{{eteacher::app_ui::layout::RegionType::List, 0}, layout.body};
                list.AttachRegion(&list_region);
                list.SetItems(&lines);
                list.SetSelected(0);
                list.Draw(gfx, d->epd);
                return;
            }
        };

        EpdManager::GetInstance().Schedule(EpdManager::TaskType::kPartial,
                                           cb,
                                           draw_ctx,
                                           [](void *ctx_ptr) { delete static_cast<DrawCtx *>(ctx_ptr); },
                                           EpdManager::Rect(0, 0, draw_ctx->epd->width(), draw_ctx->epd->height()));
    }

private:
    void HandleMainMenu(AppContext &ctx, const ButtonEvent &event) {
        if (event.id == AppButton::Left) {
            main_index_ = (main_index_ - 1 + kMainMenuCount) % kMainMenuCount;
            Render(ctx);
            return;
        }
        if (event.id == AppButton::Right) {
            main_index_ = (main_index_ + 1) % kMainMenuCount;
            Render(ctx);
            return;
        }
        if (event.id == AppButton::C) {
            if (main_index_ == 0) {
                mode_ = ViewMode::WifiSub;
            } else if (main_index_ == 1) {
                mode_ = ViewMode::Language;
            } else if (main_index_ == 2) {
                mode_ = ViewMode::User;
            } else if (main_index_ == 3) {
                mode_ = ViewMode::Learning;
            } else {
                mode_ = ViewMode::DeviceInfo;
            }
            Render(ctx);
            return;
        }
    }

    void HandleWifiSub(AppContext &ctx, const ButtonEvent &event) {
        if (event.id == AppButton::Up) {
            wifi_sub_index_ = (wifi_sub_index_ - 1 + 2) % 2;
            Render(ctx);
            return;
        }
        if (event.id == AppButton::Down) {
            wifi_sub_index_ = (wifi_sub_index_ + 1) % 2;
            Render(ctx);
            return;
        }
        if (event.id == AppButton::C) {
            if (wifi_sub_index_ == 0) {
                mode_ = ViewMode::WifiAdd;
                wifi_add_focus_ = WifiAddFocus::ScanButton;
                status_msg_.clear();
                connect_msg_.clear();
                Render(ctx);
            } else {
                mode_ = ViewMode::WifiSaved;
                LoadSavedList();
                Render(ctx);
            }
            return;
        }
        if (event.id == AppButton::B) {
            mode_ = ViewMode::MainMenu;
            Render(ctx);
            return;
        }
    }

    void HandleWifiAdd(AppContext &ctx, const ButtonEvent &event) {
        bool consumed = false;
        if (event.id == AppButton::B) {
            if (wifi_add_focus_ == WifiAddFocus::Keyboard && password_.empty()) {
                wifi_add_focus_ = WifiAddFocus::ScanList;
                Render(ctx);
                return;
            }
            if (!password_.empty()) {
                password_.pop_back();
                Render(ctx);
                return;
            }
            mode_ = ViewMode::WifiSub;
            Render(ctx);
            return;
        }

        // Focus change for scan/list/keyboard
        if (wifi_add_focus_ != WifiAddFocus::Keyboard) {
            if (event.id == AppButton::Left) {
                wifi_add_focus_ = PrevFocus(wifi_add_focus_);
                Render(ctx);
                return;
            }
            if (event.id == AppButton::Right) {
                wifi_add_focus_ = NextFocus(wifi_add_focus_);
                Render(ctx);
                return;
            }
        }

        if (wifi_add_focus_ == WifiAddFocus::Keyboard) {
            std::string output;
            if (keyboard_.HandleButton(event, output, &consumed)) {
                // handled in callback
                Render(ctx);
                return;
            }
            if (consumed) {
                Render(ctx);
                return;
            }
        }

        if (event.id == AppButton::Up) {
            if (wifi_add_focus_ == WifiAddFocus::ScanList && !scan_results_.empty()) {
                scan_selected_ = (scan_selected_ - 1 + static_cast<int>(scan_results_.size())) % static_cast<int>(scan_results_.size());
                Render(ctx);
                return;
            }
        }
        if (event.id == AppButton::Down) {
            if (wifi_add_focus_ == WifiAddFocus::ScanList && !scan_results_.empty()) {
                scan_selected_ = (scan_selected_ + 1) % static_cast<int>(scan_results_.size());
                Render(ctx);
                return;
            }
        }
        if (event.id == AppButton::C) {
            if (wifi_add_focus_ == WifiAddFocus::ScanButton) {
                ScanWifi();
                Render(ctx);
                return;
            }
            ConnectSelected();
            Render(ctx);
            return;
        }
    }

    void HandleWifiSaved(AppContext &ctx, const ButtonEvent &event) {
        if (event.id == AppButton::B) {
            mode_ = ViewMode::WifiSub;
            Render(ctx);
            return;
        }
        if (event.id == AppButton::Up && !saved_results_.empty()) {
            saved_selected_ = (saved_selected_ - 1 + static_cast<int>(saved_results_.size())) % static_cast<int>(saved_results_.size());
            Render(ctx);
            return;
        }
        if (event.id == AppButton::Down && !saved_results_.empty()) {
            saved_selected_ = (saved_selected_ + 1) % static_cast<int>(saved_results_.size());
            Render(ctx);
            return;
        }
    }

    void HandleSimplePage(AppContext &ctx, const ButtonEvent &event) {
        if (event.id == AppButton::B) {
            mode_ = ViewMode::MainMenu;
            Render(ctx);
        }
    }

    WifiAddFocus NextFocus(WifiAddFocus f) const {
        if (f == WifiAddFocus::ScanButton) return WifiAddFocus::ScanList;
        if (f == WifiAddFocus::ScanList) return WifiAddFocus::Keyboard;
        return WifiAddFocus::ScanButton;
    }

    WifiAddFocus PrevFocus(WifiAddFocus f) const {
        if (f == WifiAddFocus::ScanButton) return WifiAddFocus::Keyboard;
        if (f == WifiAddFocus::ScanList) return WifiAddFocus::ScanButton;
        return WifiAddFocus::ScanList;
    }

    void LoadSavedList() {
        saved_results_.clear();
        auto &mgr = SsidManager::GetInstance();
        for (const auto &item : mgr.GetSsidList()) {
            saved_results_.push_back(item.ssid);
        }
        saved_selected_ = 0;
    }

    void ScanWifi() {
        status_msg_.clear();
        scan_results_.clear();
        scan_selected_ = 0;

        auto &wifi = WifiManager::GetInstance();
        if (!wifi.IsInitialized()) {
            WifiManagerConfig cfg;
            cfg.ssid_prefix = "Xiaozhi";
            cfg.language = "zh-CN";
            wifi.Initialize(cfg);
        }

        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();

        wifi_scan_config_t config = {};
        config.show_hidden = true;
        esp_err_t err = esp_wifi_scan_start(&config, true);
        if (err != ESP_OK) {
            status_msg_ = "扫描失败";
            ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
            return;
        }
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        if (ap_num == 0) {
            status_msg_ = "未找到网络";
            return;
        }
        std::vector<wifi_ap_record_t> records(ap_num);
        esp_wifi_scan_get_ap_records(&ap_num, records.data());
        std::sort(records.begin(), records.end(), [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
            return a.rssi > b.rssi;
        });

        for (const auto &rec : records) {
            if (rec.ssid[0] == '\0') continue;
            std::string ssid(reinterpret_cast<const char*>(rec.ssid));
            if (std::find(scan_results_.begin(), scan_results_.end(), ssid) == scan_results_.end()) {
                scan_results_.push_back(ssid);
            }
        }
        if (scan_results_.empty()) {
            status_msg_ = "未找到网络";
        } else {
            status_msg_ = "扫描完成";
        }
    }

    void ConnectSelected() {
        if (scan_results_.empty()) {
            status_msg_ = "请先扫描";
            return;
        }
        if (scan_selected_ < 0 || scan_selected_ >= static_cast<int>(scan_results_.size())) {
            status_msg_ = "请选择网络";
            return;
        }
        if (password_.empty()) {
            status_msg_ = "请输入密码";
            return;
        }
        const std::string &ssid = scan_results_[scan_selected_];
        SsidManager::GetInstance().AddSsid(ssid, password_);
        WifiManager::GetInstance().StartStation();
        connecting_ = true;
        connect_start_ms_ = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        status_msg_ = "连接中...";
    }

private:
    static constexpr int kMainMenuCount = 5;

    AppContext *ctx_ = nullptr;
    ViewMode mode_ = ViewMode::MainMenu;
    int main_index_ = 0;
    int wifi_sub_index_ = 0;
    WifiAddFocus wifi_add_focus_ = WifiAddFocus::ScanButton;

    std::vector<std::string> scan_results_;
    std::vector<std::string> saved_results_;
    int scan_selected_ = 0;
    int saved_selected_ = 0;

    std::string password_;
    std::string status_msg_;
    std::string connect_msg_;

    bool connecting_ = false;
    uint32_t connect_start_ms_ = 0;
    uint32_t last_tick_ms_ = 0;
    eteacher::app_menu::MenuStatus last_status_{};

    eteacher::app_ui::KeyboardWidget keyboard_;
};

DeviceSettingApp::DeviceSettingApp() : impl_(std::make_unique<Impl>()) {}

MenuMeta DeviceSettingApp::GetMenuMeta() const {
    return MenuMeta{"device_setting", "系统设置", ""};
}

void DeviceSettingApp::OnEnter(AppContext &ctx) {
    if (impl_) {
        impl_->OnEnter(ctx);
    }
}

void DeviceSettingApp::OnExit(AppContext &ctx) {
    if (impl_) {
        impl_->OnExit(ctx);
    }
}

void DeviceSettingApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
    if (impl_) {
        impl_->OnButton(ctx, event);
    }
}

void DeviceSettingApp::OnTick(AppContext &ctx) {
    if (impl_) {
        impl_->OnTick(ctx);
    }
}

void DeviceSettingApp::Render(AppContext &ctx) {
    if (impl_) {
        impl_->Render(ctx);
    }
}

std::unique_ptr<AppBase> MakeDeviceSettingApp() {
    return std::make_unique<DeviceSettingApp>();
}
