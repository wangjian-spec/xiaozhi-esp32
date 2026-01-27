#include "eteacher/app_manager/app_base.h"
#include "eteacher/app_service/tool/tabs.h"
#include "eteacher/app_service/tool/soft_keyboard.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"

#include <vector>
#include <string>
#include <algorithm>
#include <memory>
#include <cstddef>

using namespace eteacher;

class DeviceSettingApp : public AppBase {
public:
    MenuMeta GetMenuMeta() const override {
        return MenuMeta{"device_setting", "系统设置 - WIFI", "方向键/ABCD"};
    }

    eteacher::layout::LayoutTemplate Template() const override {
        return mode_ == Mode::kEnteringCredentials
                   ? eteacher::layout::LayoutTemplate::InputKeyboard()
                   : eteacher::layout::LayoutTemplate::FocusContent();
    }

    void OnEnter(AppContext &ctx) override {
        board_ = &ctx.board;
        auto *epd = dynamic_cast<CustomEpdDisplay *>(board_->GetDisplay());
        if (!epd) {
            ctx.board.GetDisplay()->SetChatMessage("system", "EPD unavailable");
            return;
        }

        // sample data (in real implementation replace with actual WiFi scan/load)
        scanned_ = {"AP_home", "AP_guest", "MyPhoneHotspot"};
        saved_ = {"AP_saved"};
        current_connected_.clear();

        // configure tabs: 1 row x 2 cols (布局会在 UpdateLayout 中更新位置与尺寸)
        TabConfig cfg;
        cfg.rows = 1;
        cfg.cols = 2;
        cfg.option_area_height = 40;
        cfg.width = epd->width();
        cfg.height = epd->height();
        tabs_ = std::make_unique<TabView>(cfg);

        std::vector<TabItem> items;
        items.push_back(TabItem{"添加/扫描", []() {}});
        items.push_back(TabItem{"已保存/状态", []() {}});
        tabs_->SetItems(items);
        tabs_->Show(epd, 0, 0, epd->width(), epd->height());

        left_index_ = 0;
        right_index_ = 0;
        mode_ = Mode::kListing;

        UpdateLayout(epd);
        RenderPropertyArea();
    }

    void OnExit(AppContext &ctx) override {
        if (tabs_) tabs_->Close();
        if (skb_.IsVisible()) skb_.CloseKeyboard();
        ctx.board.GetDisplay()->SetChatMessage("system", "");
    }

    void OnButton(AppContext &ctx, const ButtonEvent &event) override {
        if (!board_) return;
        auto *epd = dynamic_cast<CustomEpdDisplay *>(board_->GetDisplay());
        if (!epd) return;

        bool consumed = false;
        if (tabs_ && tabs_->HandleButton(event, &consumed)) {
            // Tab navigation consumed (entering/exiting property view)
            // If entered property view, render property area.
            RenderPropertyArea();
            return;
        }

        // If soft keyboard visible, forward key events to it first.
        if (skb_.IsVisible()) {
            std::string out;
            bool sk_consumed = false;
            bool confirmed = skb_.HandleButton(event, out, &sk_consumed);
            if (confirmed) {
                // character confirmed
                if (!out.empty()) {
                    entering_password_ += out;
                }
            }

            bool need_render = confirmed;
            if (skb_.ConsumeStateChanged()) {
                need_render = true;
            }
            if (need_render) {
                RenderPasswordEntry(epd);
            }

            if (confirmed || sk_consumed) return;

            // confirm (C) to submit password
            if (event.action == ButtonAction::Click && event.id == AppButton::C) {
                // pretend to connect: success
                current_connected_ = editing_ssid_;
                if (std::find(saved_.begin(), saved_.end(), editing_ssid_) == saved_.end()) {
                    saved_.push_back(editing_ssid_);
                }
                skb_.CloseKeyboard();
                skb_.ConsumeStateChanged();
                mode_ = Mode::kListing;
                UpdateLayout(epd);
                RenderPropertyArea();
                return;
            }
        }

        // If property view is showing, handle internal navigation and actions.
        if (tabs_ && tabs_->IsShowingProperties()) {
            int sel = tabs_->SelectedIndex();
            if (event.action == ButtonAction::Click) {
                if (event.id == AppButton::Up) {
                    if (sel == 0) { // left: Add/Scanned
                        if (left_index_ > 0) --left_index_; else left_index_ = (int)scanned_.size();
                    } else { // right: saved/current
                        if (right_index_ > 0) --right_index_;
                    }
                    RenderPropertyArea();
                    return;
                }
                if (event.id == AppButton::Down) {
                    if (sel == 0) {
                        // left_index_ == 0 means "Add WiFi", following entries are scanned_
                        ++left_index_;
                        if (left_index_ > (int)scanned_.size()) left_index_ = 0;
                    } else {
                        ++right_index_;
                        if (right_index_ >= (int)saved_.size()) right_index_ = (int)saved_.size() - 1;
                    }
                    RenderPropertyArea();
                    return;
                }
                if (event.id == AppButton::B) {
                    // If on right panel (saved networks), B deletes the selected network.
                    if (sel == 1) {
                        if (!saved_.empty() && right_index_ >= 0 && right_index_ < (int)saved_.size()) {
                            saved_.erase(saved_.begin() + right_index_);
                            if (right_index_ >= (int)saved_.size()) right_index_ = std::max(0, (int)saved_.size() - 1);
                            RenderPropertyArea();
                        }
                        return;
                    }
                    // Otherwise, B exits property view
                    tabs_->Back();
                    RenderPropertyArea();
                    return;
                }
                if (event.id == AppButton::C) {
                    // Confirm inside property view
                    if (sel == 0) {
                        // left: index 0 -> Add WiFi, else scanned list
                        if (left_index_ == 0) {
                            // Enter manual add (ask SSID then password) - for brevity we'll treat as manual password entry
                            editing_ssid_.clear();
                            entering_password_.clear();
                            mode_ = Mode::kEnteringCredentials;
                            UpdateLayout(epd);
                            // show keyboard for password input (按 Secondary 区域布局)
                            ShowKeyboardInSecondary(epd);
                            skb_.ConsumeStateChanged();
                        } else {
                            // scanned entry selected. left_index_ - 1 maps into scanned_
                            int idx = left_index_ - 1;
                            if (idx >= 0 && idx < (int)scanned_.size()) {
                                editing_ssid_ = scanned_[idx];
                                entering_password_.clear();
                                mode_ = Mode::kEnteringCredentials;
                                UpdateLayout(epd);
                                ShowKeyboardInSecondary(epd);
                                skb_.ConsumeStateChanged();
                            }
                        }
                        RenderPropertyArea();
                        return;
                    } else {
                        // right side: select saved network -> connect
                        if (!saved_.empty() && right_index_ >= 0 && right_index_ < (int)saved_.size()) {
                            editing_ssid_ = saved_[right_index_];
                            // simulate connect
                            current_connected_ = editing_ssid_;
                            RenderPropertyArea();
                        }
                        return;
                    }
                }
            }
        }
    }

private:
    enum class Mode { kListing, kEnteringCredentials };

    Board* board_ = nullptr;
    std::unique_ptr<TabView> tabs_;
    SoftKeyboard skb_;

    std::vector<std::string> scanned_;
    std::vector<std::string> saved_;
    std::string current_connected_;

    int left_index_ = 0;  // 0 == Add WiFi, 1..N = scanned list
    int right_index_ = 0; // index into saved_

    Mode mode_ = Mode::kListing;
    std::string editing_ssid_;
    std::string entering_password_;

    eteacher::layout::LayoutParams layout_params_{};
    eteacher::layout::LayoutResult layout_result_{};

    void RenderPropertyArea() {
        if (!board_) return;
        auto *epd = dynamic_cast<CustomEpdDisplay *>(board_->GetDisplay());
        if (!epd) return;

        struct DrawCtx {
            CustomEpdDisplay *epd;
            TabView* tabs;
            SoftKeyboard* keyboard;
            std::vector<std::string> scanned;
            std::vector<std::string> saved;
            std::string current;
            int left_index;
            int right_index;
            Mode mode;
            std::string editing_ssid;
            std::string entering_password;
            eteacher::layout::LayoutResult layout;
        };

        auto *ctx = new DrawCtx{epd, tabs_.get(), &skb_, scanned_, saved_, current_connected_, left_index_, right_index_, mode_, editing_ssid_, entering_password_, layout_result_};

        auto cb = [](Adafruit_GFX &gfx, void *v) {
            auto *d = static_cast<DrawCtx *>(v);
            if (!d || !d->epd) return;
            gfx.fillRect(d->epd->width() > 0 ? 0 : 0, d->epd->height() > 0 ? 0 : 0, d->epd->width(), d->epd->height(), GxEPD_WHITE);

            const auto *header = d->layout.Find({eteacher::layout::RegionType::Header, 0});
            const auto *primary = d->layout.Find({eteacher::layout::RegionType::Primary, 0});
            const auto *secondary = d->layout.Find({eteacher::layout::RegionType::Secondary, 0});
            const auto *footer = d->layout.Find({eteacher::layout::RegionType::Footer, 0});

            if (d->tabs && header && !header->rect.IsEmpty()) {
                d->tabs->Draw(gfx);
                int16_t title_x = static_cast<int16_t>(header->rect.x + 6);
                int16_t title_y = static_cast<int16_t>(header->rect.y + header->rect.h - 6);
                d->epd->DrawUtf8(title_x, title_y, "WIFI 设置", "wenquanyi_11pt", GxEPD_BLACK);
            }

            if (primary && !primary->rect.IsEmpty()) {
                const int padding = 6;
                int content_x = primary->rect.x + padding;
                int content_y = primary->rect.y + padding + 10;
                int content_w = primary->rect.w - padding * 2;

                if (d->mode == Mode::kEnteringCredentials) {
                    d->epd->DrawUtf8(content_x, content_y, ("SSID: " + d->editing_ssid).c_str(), "wenquanyi_9pt", GxEPD_BLACK);
                    d->epd->DrawUtf8(content_x, content_y + 16, ("密码: " + d->entering_password).c_str(), "wenquanyi_9pt", GxEPD_BLACK);
                    d->epd->DrawUtf8(content_x, content_y + 36, "C 确认连接  B 返回", "wenquanyi_9pt", GxEPD_BLACK);
                } else {
                    int left_w = content_w / 2;
                    int right_w = content_w - left_w;
                    int left_x = content_x;
                    int right_x = content_x + left_w + 6;

                    int ly = content_y;
                    d->epd->DrawUtf8(left_x, ly, "左: 添加/扫描", "wenquanyi_9pt", GxEPD_BLACK);
                    ly += 14;
                    int list_x = left_x;
                    if (d->left_index == 0) {
                        gfx.fillRect(list_x - 2, ly - 2, left_w - 8, 16, GxEPD_BLACK);
                        d->epd->DrawUtf8(list_x, ly + 10, "[添加WIFI]", "wenquanyi_9pt", GxEPD_WHITE);
                    } else {
                        d->epd->DrawUtf8(list_x, ly + 10, "[添加WIFI]", "wenquanyi_9pt", GxEPD_BLACK);
                    }
                    ly += 18;
                    for (std::size_t i = 0; i < d->scanned.size(); ++i) {
                        const bool sel = (d->left_index == (int)i + 1);
                        if (sel) gfx.fillRect(list_x - 2, ly - 2, left_w - 6, 16, GxEPD_BLACK);
                        d->epd->DrawUtf8(list_x, ly + 10, d->scanned[i].c_str(), "wenquanyi_9pt", sel ? GxEPD_WHITE : GxEPD_BLACK);
                        ly += 16;
                    }

                    int ry = content_y;
                    d->epd->DrawUtf8(right_x, ry, "右: 已保存 / 当前", "wenquanyi_9pt", GxEPD_BLACK);
                    ry += 14;
                    std::string conn = d->current.empty() ? "未连接" : std::string("连接: ") + d->current;
                    d->epd->DrawUtf8(right_x, ry + 10, conn.c_str(), "wenquanyi_9pt", GxEPD_BLACK);
                    ry += 18;
                    for (std::size_t i = 0; i < d->saved.size(); ++i) {
                        const bool sel = (d->right_index == (int)i);
                        if (sel) gfx.fillRect(right_x - 2, ry - 2, right_w - 6, 16, GxEPD_BLACK);
                        d->epd->DrawUtf8(right_x, ry + 10, d->saved[i].c_str(), "wenquanyi_9pt", sel ? GxEPD_WHITE : GxEPD_BLACK);
                        ry += 16;
                    }
                }
            }

            if (footer && !footer->rect.IsEmpty()) {
                int fx = footer->rect.x + 6;
                int fy = footer->rect.y + footer->rect.h - 6;
                if (d->mode == Mode::kEnteringCredentials) {
                    d->epd->DrawUtf8(fx, fy, "键盘区位于下方", "wenquanyi_9pt", GxEPD_BLACK);
                } else {
                    d->epd->DrawUtf8(fx, fy, "左右选择面板  上下选择网络  C 确认  B 返回", "wenquanyi_9pt", GxEPD_BLACK);
                }
            }

            if (secondary && d->keyboard && d->keyboard->IsVisible()) {
                d->keyboard->Draw(gfx);
            }
        };

        EpdManager::GetInstance().Schedule(EpdManager::TaskType::kPartial, cb, ctx, [](void *v) {
            delete static_cast<DrawCtx *>(v);
        }, EpdManager::Rect(0, 0, epd->width(), epd->height()));
    }

    void RenderPasswordEntry(CustomEpdDisplay *epd) {
        RenderPropertyArea();
    }

    void UpdateLayout(CustomEpdDisplay *epd) {
        if (!epd) {
            return;
        }
        eteacher::layout::ScreenInfo screen;
        screen.width = static_cast<int16_t>(epd->width());
        screen.height = static_cast<int16_t>(epd->height());
        screen.orientation = eteacher::layout::Orientation::Auto;

        layout_params_ = eteacher::layout::LayoutParams{};
        layout_params_.margin = {6, 6, 6, 6};
        layout_params_.gap = 4;
        layout_params_.header_height = eteacher::layout::SizeSpec::Px(36);
        layout_params_.footer_height = eteacher::layout::SizeSpec::Px(20);
        layout_params_.primary_count = 1;
        layout_params_.secondary_count = (mode_ == Mode::kEnteringCredentials) ? 1 : 0;
        layout_params_.secondary_height = eteacher::layout::SizeSpec::Percent(40);
        layout_params_.partial_refresh_default = true;

        layout_result_ = eteacher::layout::LayoutEngine::Compute(Template(), screen, layout_params_);

        const auto *header = layout_result_.Find({eteacher::layout::RegionType::Header, 0});
        if (tabs_ && header && !header->rect.IsEmpty()) {
            TabConfig cfg;
            cfg.rows = 1;
            cfg.cols = 2;
            cfg.option_area_height = header->rect.h;
            cfg.width = header->rect.w;
            cfg.height = header->rect.h;
            cfg.x = header->rect.x;
            cfg.y = header->rect.y;
            tabs_->SetConfig(cfg);
            tabs_->Show(epd, header->rect.x, header->rect.y, header->rect.w, header->rect.h);
        }
    }

    void ShowKeyboardInSecondary(CustomEpdDisplay *epd) {
        if (!epd) {
            return;
        }
        const auto *secondary = layout_result_.Find({eteacher::layout::RegionType::Secondary, 0});
        if (secondary && !secondary->rect.IsEmpty()) {
            skb_.ShowKeyboard(epd, secondary->rect.x, secondary->rect.y, secondary->rect.w, secondary->rect.h);
        } else {
            skb_.ShowKeyboard(epd);
        }
    }
};

// Factory function used by the app manager elsewhere.
std::unique_ptr<AppBase> MakeDeviceSettingApp() {
    return std::unique_ptr<AppBase>(new DeviceSettingApp());
}
