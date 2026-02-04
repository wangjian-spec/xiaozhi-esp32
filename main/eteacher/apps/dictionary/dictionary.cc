#include "eteacher/apps/dictionary/dictionary.h"

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "eteacher/font_manager/font_manager.h"
#include "eteacher/app_ui/renderer.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/widget_builder.h"

#include "display.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cJSON.h>

namespace {

static constexpr const char *kStatusFont = "wenquanyi_9pt";

extern const uint8_t kDictionaryJsonStart[] asm("_binary_dictionary_json_start");
extern const uint8_t kDictionaryJsonEnd[] asm("_binary_dictionary_json_end");

cJSON* LoadEmbeddedJson(const uint8_t* start, const uint8_t* end) {
    if (!start || !end || end <= start) {
        return nullptr;
    }
    const size_t size = static_cast<size_t>(end - start);
    std::string json(reinterpret_cast<const char*>(start), size);
    return cJSON_Parse(json.c_str());
}

int GetFontHeight(std::string_view name) {
    const auto *font = eteacher::font_manager::GetBuiltinFont(name);
    if (!font) {
        return 16;
    }
    return static_cast<int>(font->Header().ascent + font->Header().descent);
}

int GetFontAscent(std::string_view name) {
    const auto *font = eteacher::font_manager::GetBuiltinFont(name);
    if (!font) {
        return 12;
    }
    return static_cast<int>(font->Header().ascent);
}

class EpdPainter : public app_ui::Painter {
public:
    EpdPainter(CustomEpdDisplay* epd, Adafruit_GFX& gfx)
        : epd_(epd), gfx_(gfx) {}

    void SetClip(const app_ui::Rect&) override {}
    void PushClip(const app_ui::Rect&) override {}
    void PopClip() override {}

    void DrawText(app_ui::Point p, const char* text) override {
        if (!epd_ || !text) {
            return;
        }
        const int16_t x = static_cast<int16_t>(p.x + offset_.x);
        const int16_t y = static_cast<int16_t>(p.y + offset_.y);
        epd_->DrawUtf8(x, y + GetFontAscent(kStatusFont), text, kStatusFont, text_color_);
    }

    app_ui::Size MeasureText(const char* text, app_ui::Font*) override {
        if (!epd_ || !text) {
            return {0, 0};
        }
        const int16_t w = epd_->MeasureUtf8Width(text, kStatusFont);
        return {w, static_cast<int16_t>(GetFontHeight(kStatusFont))};
    }

    void DrawRect(const app_ui::Rect& rect) override {
        gfx_.drawRect(rect.x + offset_.x, rect.y + offset_.y, rect.w, rect.h, draw_color_);
    }

    void FillRect(const app_ui::Rect& rect) override {
        gfx_.fillRect(rect.x + offset_.x, rect.y + offset_.y, rect.w, rect.h, draw_color_);
    }

    void DrawImage(app_ui::Point, const app_ui::Image*) override {}

    void SetFont(app_ui::Font*) override {}

    void SetDrawColor(app_ui::Color color) override {
        draw_color_ = (color == app_ui::Color::Black) ? GxEPD_BLACK : GxEPD_WHITE;
    }

    void SetTextColor(app_ui::Color color) override {
        text_color_ = (color == app_ui::Color::Black) ? GxEPD_BLACK : GxEPD_WHITE;
    }

    void DrawCircle(app_ui::Point center, int radius) override {
        gfx_.drawCircle(center.x + offset_.x, center.y + offset_.y, radius, draw_color_);
    }

    void SetTransform(const app_ui::Point& offset) override {
        offset_ = offset;
    }

    void SetAlpha(float) override {}

private:
    CustomEpdDisplay* epd_ = nullptr;
    Adafruit_GFX& gfx_;
    app_ui::Point offset_{};
    uint16_t draw_color_ = GxEPD_BLACK;
    uint16_t text_color_ = GxEPD_BLACK;
};

struct DrawCtx {
    CustomEpdDisplay* epd;
    app_ui::Widget* root;
};

void RenderWidgetTree(CustomEpdDisplay* epd, app_ui::Widget* root) {
    if (!epd || !root) {
        return;
    }

    auto cb = [](Adafruit_GFX& gfx, void* ctx) {
        auto* d = static_cast<DrawCtx*>(ctx);
        if (!d || !d->epd || !d->root) {
            return;
        }

        gfx.fillScreen(GxEPD_WHITE);

        app_ui::LayoutEngine layout_engine;
        const app_ui::Rect viewport{0, 0, static_cast<int16_t>(d->epd->width()),
                                    static_cast<int16_t>(d->epd->height())};
        d->root->SetRectInParent(viewport);
        layout_engine.LayoutTree(d->root, viewport);

        app_ui::RenderList list;
        list.Build(d->root);

        app_ui::DirtyTracker dirty;
        dirty.Add(viewport, app_ui::DirtyReason::Full);

        app_ui::Renderer renderer;
        EpdPainter painter(d->epd, gfx);
        renderer.Render(list, dirty, painter);
    };

    auto* ctx = new DrawCtx{epd, root};
    EpdManager::GetInstance().Schedule(
        EpdManager::TaskType::kPartial,
        cb,
        ctx,
        [](void* p) { delete static_cast<DrawCtx*>(p); },
        EpdManager::Rect(0, 0, epd->width(), epd->height()));
}

} // namespace

MenuMeta DictionaryApp::GetMenuMeta() const {
    return MenuMeta{"dictionary", "词典", "示例"};
}

void DictionaryApp::OnEnter(AppContext &ctx) {
    if (ui_root_) {
        cJSON_Delete(ui_root_);
        ui_root_ = nullptr;
    }

    ui_root_ = LoadEmbeddedJson(kDictionaryJsonStart, kDictionaryJsonEnd);
    if (!ui_root_) {
        printf("[Dictionary] Embedded JSON parse FAILED\n");
    } else {
        printf("[Dictionary] Embedded JSON parsed successfully\n");
    }

    scene_ids_.clear();
    if (ui_root_) {
        const cJSON* scenes = cJSON_GetObjectItemCaseSensitive(ui_root_, "scenes");
        if (cJSON_IsObject(scenes)) {
            const cJSON* scene = nullptr;
            cJSON_ArrayForEach(scene, scenes) {
                if (scene && scene->string) {
                    scene_ids_.push_back(scene->string);
                }
            }
        }
    }

    scene_index_ = 0;
    Render(ctx);
}

void DictionaryApp::OnExit(AppContext &ctx) {
    if (ui_root_) {
        cJSON_Delete(ui_root_);
        ui_root_ = nullptr;
    }
    scene_ids_.clear();
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Dictionary UI");
}

void DictionaryApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
    if (event.action != ButtonAction::Click) {
        return;
    }

    switch (event.id) {
    case AppButton::Up:
    case AppButton::Left:
        PrevScene(ctx);
        break;
    case AppButton::Down:
    case AppButton::Right:
        NextScene(ctx);
        break;
    case AppButton::Start:
    case AppButton::A:
        Render(ctx);
        break;
    default:
        break;
    }
}

void DictionaryApp::PrevScene(AppContext &ctx) {
    if (scene_ids_.empty()) {
        return;
    }
    scene_index_ = (scene_index_ - 1 + static_cast<int>(scene_ids_.size())) % static_cast<int>(scene_ids_.size());
    Render(ctx);
}

void DictionaryApp::NextScene(AppContext &ctx) {
    if (scene_ids_.empty()) {
        return;
    }
    scene_index_ = (scene_index_ + 1) % static_cast<int>(scene_ids_.size());
    Render(ctx);
}

void DictionaryApp::Render(AppContext &ctx) {
    if (!ui_root_) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: json missing");
        return;
    }

    if (scene_ids_.empty()) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: no scenes");
        return;
    }

    const std::string& scene_id = scene_ids_[scene_index_];
    if (!scene_mgr_.LoadFromJson(ui_root_, scene_id.c_str(), static_cast<uint16_t>(scene_index_))) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: failed to load scene");
        return;
    }

    if (auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay())) {
        RenderWidgetTree(epd, scene_mgr_.Root());
        return;
    }

    std::string msg = "Dictionary UI\n";
    msg += "Scene: ";
    msg += "#";
    msg += std::to_string(scene_mgr_.SceneId());
    msg += "\nUse Up/Down to switch";
    ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
}

std::unique_ptr<AppBase> MakeDictionaryApp() {
    return std::make_unique<DictionaryApp>();
}
