#include "eteacher/apps/dictionary/dictionary.h"
#include "eteacher/app_service/tool/soft_keyboard.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"

#include <memory>
#include <string>

using namespace eteacher;

class DictionaryAppImpl : public DictionaryApp {
public:
    MenuMeta GetMenuMeta() const override {
        return MenuMeta{"dictionary", "软键盘测试", "C 打开/确认  A 切换大小写  B 关闭"};
    }

    void OnEnter(AppContext &ctx) override {
        board_ = &ctx.board;
        RenderMain();
    }

    void OnExit(AppContext &ctx) override {
        if (skb_.IsVisible()) skb_.CloseKeyboard();
        if (board_) board_->GetDisplay()->SetChatMessage("", "");
    }

    void OnButton(AppContext &ctx, const ButtonEvent &event) override {
        if (!board_) return;
        auto *epd = dynamic_cast<CustomEpdDisplay *>(board_->GetDisplay());
        if (!epd) return;

        // If soft keyboard visible, forward events first
        if (skb_.IsVisible()) {
            std::string out;
            bool consumed = false;
            bool confirmed = skb_.HandleButton(event, out, &consumed);
            if (confirmed && !out.empty()) {
                typed_ += out;
            }

            bool need_render = confirmed;
            if (skb_.ConsumeStateChanged()) {
                need_render = true;
            }
            if (need_render) {
                RenderMain();
            }

            if (confirmed || consumed) return;

            // B closes keyboard
            if (event.action == ButtonAction::Click && event.id == AppButton::B) {
                skb_.CloseKeyboard();
                skb_.ConsumeStateChanged();
                RenderMain();
                return;
            }
            return;
        }

        // When keyboard not visible, C opens it
        if (event.action == ButtonAction::Click && event.id == AppButton::C) {
            skb_.ShowKeyboard(epd);
            skb_.ConsumeStateChanged();
            RenderMain();
            return;
        }
    }

private:
    Board* board_ = nullptr;
    SoftKeyboard skb_;
    std::string typed_;

    void RenderMain() {
        if (!board_) return;
        auto *epd = dynamic_cast<CustomEpdDisplay *>(board_->GetDisplay());
        if (!epd) return;

        struct Ctx {
            CustomEpdDisplay* epd;
            std::string typed;
            bool keyboard_visible;
            SoftKeyboard* keyboard;
        };

        auto *ctx = new Ctx{epd, typed_, skb_.IsVisible(), &skb_};

        auto cb = [](Adafruit_GFX &gfx, void *v) {
            auto *c = static_cast<Ctx *>(v);
            if (!c || !c->epd) return;
            gfx.fillRect(0, 0, c->epd->width(), c->epd->height(), GxEPD_WHITE);
            int x = 8;
            int y = 18;
            c->epd->DrawUtf8(x, y, "软键盘测试", "wenquanyi_11pt", GxEPD_BLACK);
            y += 22;
            c->epd->DrawUtf8(x, y, "说明: 按 C 打开软键盘，A 切换大小写，D 确认字符，B 关闭", "wenquanyi_9pt", GxEPD_BLACK);
            y += 18;
            c->epd->DrawUtf8(x, y, "输入内容:", "wenquanyi_9pt", GxEPD_BLACK);
            y += 14;
            // Truncate typed for display
            std::string display = c->typed;
            if ((int)display.size() > 40) display = display.substr(display.size() - 40);
            c->epd->DrawUtf8(x, y, display.c_str(), "wenquanyi_9pt", GxEPD_BLACK);
            y += 18;
            if (c->keyboard_visible) {
                c->epd->DrawUtf8(x, y, "软键盘已打开，使用方向键/连击导航，D 确认字符。", "wenquanyi_9pt", GxEPD_BLACK);
            } else {
                c->epd->DrawUtf8(x, y, "软键盘已关闭。按 C 打开。", "wenquanyi_9pt", GxEPD_BLACK);
            }

            if (c->keyboard && c->keyboard->IsVisible()) {
                c->keyboard->Draw(gfx);
            }
        };

        EpdManager::GetInstance().Schedule(EpdManager::TaskType::kPartial, cb, ctx, [](void *v) { delete static_cast<Ctx *>(v); }, EpdManager::Rect(0, 0, epd->width(), epd->height()));
    }
};

std::unique_ptr<AppBase> MakeDictionaryApp() {
    return std::unique_ptr<AppBase>(new DictionaryAppImpl());
}
