#include "eteacher/apps/device_setting/device_setting.h"

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "eteacher/font_manager/font_manager.h"
#include "eteacher/app_ui/renderer.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/widget_builder.h"

#include "display.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <cJSON.h>
#include <cstdint>
#include <cerrno>
#include <cstring>

namespace {

static constexpr const char *kStatusFont = "wenquanyi_9pt";
static constexpr int kPadding = 8;

// Embedded JSON removed — UI JSON is loaded from flash at runtime.

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

MenuMeta DeviceSettingApp::GetMenuMeta() const {
    return MenuMeta{"device_setting", "系统设置", "示例"};
}

// Try loading UI JSON from common flash paths (SPIFFS/LittleFS). Returns parsed cJSON or nullptr.
static cJSON* LoadJsonFromFlash() {
    const char* candidates[] = {
        "/spiffs/device_setting.json",
        "/spiffs/ui_device_setting.json",
        "/spiffs/ui.json",
        "/device_setting.json",
        "/ui_device_setting.json",
        "/ui.json",
    };
    for (const char* path : candidates) {
        FILE* f = fopen(path, "rb");
        if (!f) {
            printf("[DeviceSetting] fopen failed for %s: %s\n", path, std::strerror(errno));
            continue;
        }
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            continue;
        }
        long size = ftell(f);
        if (size <= 0) {
            fclose(f);
            continue;
        }
        rewind(f);
        std::string buf;
        buf.resize(static_cast<size_t>(size));
        size_t read = fread(&buf[0], 1, buf.size(), f);
        fclose(f);
        if (read != buf.size()) {
            continue;
        }
        cJSON* root = cJSON_Parse(buf.c_str());
        if (root) {
            printf("[DeviceSetting] Loaded UI JSON from: %s\n", path);
            return root;
        }
        // Print a short snippet to help debugging parse failures
        size_t show = std::min<size_t>(buf.size(), 256);
        printf("[DeviceSetting] Failed parse for: %s (first %zu bytes):\n%.*s\n",
               path, show, static_cast<int>(show), buf.c_str());
    }
    return nullptr;
}

// Try to load JSON embedded into the firmware via CMake EMBED_FILES.
static cJSON* LoadEmbeddedJson() {
    auto parse_embedded = [](const uint8_t* begin, const uint8_t* end, const char* name) -> cJSON* {
        if (!begin || !end || end <= begin) {
            return nullptr;
        }
        size_t len = static_cast<size_t>(end - begin);
        std::string_view sv(reinterpret_cast<const char*>(begin), len);
        cJSON* root = cJSON_ParseWithLength(sv.data(), len);
        if (root) {
            printf("[DeviceSetting] Loaded embedded UI JSON: %s\n", name);
            return root;
        }
        printf("[DeviceSetting] Failed parse embedded %s (first 256 bytes):\n%.*s\n",
               name, static_cast<int>(std::min<size_t>(len, 256)), sv.data());
        return nullptr;
    };

    // Symbols are generated by the linker for each EMBED_FILES entry.
    // Pattern: _binary_<relpath_with_underscores>_start / _end
    // Our CMake globs use: eteacher/apps/jsons/ui_json/<name>.json
    // So symbol names become: _binary_eteacher_apps_jsons_ui_json_<name>_json_start
    {
        extern const uint8_t _binary_eteacher_apps_jsons_ui_json_device_setting_json_start[] __attribute__((weak));
        extern const uint8_t _binary_eteacher_apps_jsons_ui_json_device_setting_json_end[] __attribute__((weak));
        if (auto* root = parse_embedded(_binary_eteacher_apps_jsons_ui_json_device_setting_json_start,
                                        _binary_eteacher_apps_jsons_ui_json_device_setting_json_end,
                                        "device_setting.json")) {
            return root;
        }
    }
    {
        extern const uint8_t _binary_device_setting_json_start[] __attribute__((weak));
        extern const uint8_t _binary_device_setting_json_end[] __attribute__((weak));
        if (auto* root = parse_embedded(_binary_device_setting_json_start,
                                        _binary_device_setting_json_end,
                                        "device_setting.json")) {
            return root;
        }
    }
    {
        extern const uint8_t _binary_eteacher_apps_jsons_ui_json_ui_device_setting_json_start[] __attribute__((weak));
        extern const uint8_t _binary_eteacher_apps_jsons_ui_json_ui_device_setting_json_end[] __attribute__((weak));
        if (auto* root = parse_embedded(_binary_eteacher_apps_jsons_ui_json_ui_device_setting_json_start,
                                        _binary_eteacher_apps_jsons_ui_json_ui_device_setting_json_end,
                                        "ui_device_setting.json")) {
            return root;
        }
    }
    {
        extern const uint8_t _binary_ui_device_setting_json_start[] __attribute__((weak));
        extern const uint8_t _binary_ui_device_setting_json_end[] __attribute__((weak));
        if (auto* root = parse_embedded(_binary_ui_device_setting_json_start,
                                        _binary_ui_device_setting_json_end,
                                        "ui_device_setting.json")) {
            return root;
        }
    }
    {
        extern const uint8_t _binary_eteacher_apps_jsons_ui_json_ui_json_start[] __attribute__((weak));
        extern const uint8_t _binary_eteacher_apps_jsons_ui_json_ui_json_end[] __attribute__((weak));
        if (auto* root = parse_embedded(_binary_eteacher_apps_jsons_ui_json_ui_json_start,
                                        _binary_eteacher_apps_jsons_ui_json_ui_json_end,
                                        "ui.json")) {
            return root;
        }
    }
    {
        extern const uint8_t _binary_ui_json_start[] __attribute__((weak));
        extern const uint8_t _binary_ui_json_end[] __attribute__((weak));
        if (auto* root = parse_embedded(_binary_ui_json_start,
                                        _binary_ui_json_end,
                                        "ui.json")) {
            return root;
        }
    }
    return nullptr;
}

void DeviceSettingApp::OnEnter(AppContext &ctx) {
    if (ui_root_) {
        cJSON_Delete(ui_root_);
        ui_root_ = nullptr;
    }

    ui_root_ = LoadJsonFromFlash();
    if (!ui_root_) {
        printf("[DeviceSetting] Failed to load UI JSON from flash, trying embedded resources\n");
        ui_root_ = LoadEmbeddedJson();
        if (!ui_root_) {
            printf("[DeviceSetting] No embedded UI JSON found or parse failed\n");
        }
    }
    // Notify user on-screen when running in host/dev environment
    if (ui_root_) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: JSON loaded");
    } else {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: failed to parse JSON");
    }
    page_ids_.clear();
    if (ui_root_) {
        const cJSON* pages = cJSON_GetObjectItemCaseSensitive(ui_root_, "pages");
        if (!cJSON_IsObject(pages)) {
            pages = cJSON_GetObjectItemCaseSensitive(ui_root_, "scenes");
        }
        if (cJSON_IsObject(pages)) {
            const cJSON* page = nullptr;
            cJSON_ArrayForEach(page, pages) {
                if (page && page->string) {
                    page_ids_.push_back(page->string);
                }
            }
        }
    }

    page_index_ = 0;
    Render(ctx);
}

void DeviceSettingApp::OnExit(AppContext &ctx) {
    if (ui_root_) {
        cJSON_Delete(ui_root_);
        ui_root_ = nullptr;
    }
    page_ids_.clear();
    ctx.board.GetDisplay()->SetChatMessage("system", "Exit Device Setting UI");
}

void DeviceSettingApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
    if (event.action != ButtonAction::Click) {
        return;
    }

    switch (event.id) {
    case AppButton::Up:
    case AppButton::Left:
        PrevPage(ctx);
        break;
    case AppButton::Down:
    case AppButton::Right:
        NextPage(ctx);
        break;
    case AppButton::Start:
    case AppButton::A:
        Render(ctx);
        break;
    default:
        break;
    }
}

void DeviceSettingApp::PrevPage(AppContext &ctx) {
    if (page_ids_.empty()) {
        return;
    }
    page_index_ = (page_index_ - 1 + static_cast<int>(page_ids_.size())) % static_cast<int>(page_ids_.size());
    Render(ctx);
}

void DeviceSettingApp::NextPage(AppContext &ctx) {
    if (page_ids_.empty()) {
        return;
    }
    page_index_ = (page_index_ + 1) % static_cast<int>(page_ids_.size());
    Render(ctx);
}

void DeviceSettingApp::Render(AppContext &ctx) {
    if (!ui_root_) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: json missing");
        return;
    }

    if (page_ids_.empty()) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: no pages");
        return;
    }

    const std::string& page_id = page_ids_[page_index_];
    if (!scene_mgr_.LoadFromJson(ui_root_, page_id.c_str(), static_cast<uint16_t>(page_index_))) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: failed to load page");
        return;
    }

    if (auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay())) {
        RenderWidgetTree(epd, scene_mgr_.Root());
        return;
    }

    std::string msg = "Device Setting UI\n";
    msg += "Page: ";
    msg += "#";
    msg += std::to_string(scene_mgr_.SceneId());
    msg += "\nUse Up/Down to switch";
    ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
}

std::unique_ptr<AppBase> MakeDeviceSettingApp() {
    return std::make_unique<DeviceSettingApp>();
}
