#include "widget_builder.h"

#include <cJSON.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "scene.h"
#include "widget.h"

namespace app_ui {

namespace {

using WidgetFactoryFn = std::function<std::unique_ptr<Widget>()>;

bool DefaultFocusable(std::string_view type);
std::unique_ptr<Widget> CreateWidgetByType(const std::string& type);
std::unique_ptr<Widget> CreateWidgetByType(WidgetType type);

std::string ToLower(std::string_view value) {
    std::string out(value.begin(), value.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

const cJSON* GetObject(const cJSON* obj, const char* key) {
    return obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : nullptr;
}

std::string GetString(const cJSON* obj, const char* key) {
    const cJSON* item = GetObject(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        return item->valuestring;
    }
    return {};
}

int GetInt(const cJSON* obj, const char* key, int fallback = 0) {
    const cJSON* item = GetObject(obj, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return fallback;
}

bool GetBool(const cJSON* obj, const char* key, bool fallback) {
    const cJSON* item = GetObject(obj, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

std::string ResolveText(const cJSON* props, const cJSON* texts) {
    if (!props || !cJSON_IsObject(props)) {
        return {};
    }
    const cJSON* text_key = GetObject(props, "textId");
    if (!text_key) {
        text_key = GetObject(props, "text");
    }
    if (!text_key) {
        text_key = GetObject(props, "TextID");
    }
    if (!text_key) {
        text_key = GetObject(props, "Text");
    }
    if (cJSON_IsString(text_key) && text_key->valuestring) {
        if (texts && cJSON_IsObject(texts)) {
            const cJSON* resolved = cJSON_GetObjectItemCaseSensitive(texts, text_key->valuestring);
            if (cJSON_IsString(resolved) && resolved->valuestring) {
                return resolved->valuestring;
            }
        }
        return text_key->valuestring;
    }
    return {};
}

uint32_t HashTextId(const char* text_id) {
    if (!text_id) {
        return 0;
    }
    uint32_t h = 0x811C9DC5;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text_id);
    while (*p) {
        h ^= *p++;
        h *= 0x01000193;
    }
    return h;
}

bool IsContainerType(std::string_view type) {
    return type == "container" || type == "frame" || type == "menu" || type == "listview" ||
           type == "tabview" || type == "dialog" || type == "softkeyboard" || type == "topbar" ||
           type == "bottombar";
}

std::unique_ptr<Widget> BuildWidgetRecursive(const cJSON* widgets_json,
                                             const cJSON* widget_json,
                                             const cJSON* texts,
                                             std::unordered_set<std::string>& stack) {
    if (!widget_json) {
        return nullptr;
    }

    const std::string type_raw = GetString(widget_json, "type");
    if (type_raw.empty()) {
        return nullptr;
    }
    const std::string type = ToLower(type_raw);

    auto widget = CreateWidgetByType(type);
    if (!widget) {
        return nullptr;
    }

    const cJSON* rect_json = GetObject(widget_json, "rect");
    if (!cJSON_IsObject(rect_json)) {
        return nullptr;
    }

    const cJSON* x_item = GetObject(rect_json, "x");
    const cJSON* y_item = GetObject(rect_json, "y");
    const cJSON* w_item = GetObject(rect_json, "w");
    const cJSON* h_item = GetObject(rect_json, "h");
    if (!cJSON_IsNumber(x_item) || !cJSON_IsNumber(y_item) ||
        !cJSON_IsNumber(w_item) || !cJSON_IsNumber(h_item)) {
        return nullptr;
    }
    Rect rect{
        static_cast<int16_t>(x_item->valueint),
        static_cast<int16_t>(y_item->valueint),
        static_cast<int16_t>(w_item->valueint),
        static_cast<int16_t>(h_item->valueint),
    };
    widget->SetRectInParent(rect);

    const cJSON* visible_item = GetObject(widget_json, "visible");
    if (visible_item && !cJSON_IsBool(visible_item)) {
        return nullptr;
    }
    const cJSON* enabled_item = GetObject(widget_json, "enabled");
    if (enabled_item && !cJSON_IsBool(enabled_item)) {
        return nullptr;
    }
    const cJSON* focusable_item = GetObject(widget_json, "focusable");
    if (focusable_item && !cJSON_IsBool(focusable_item)) {
        return nullptr;
    }

    const bool visible = GetBool(widget_json, "visible", true);
    const bool enabled = GetBool(widget_json, "enabled", true);
    const bool focusable = GetBool(widget_json, "focusable", DefaultFocusable(type));

    widget->SetVisible(visible);
    widget->SetEnabled(enabled);
    widget->SetFocusable(focusable);

    const std::string direct_text = GetString(widget_json, "text");
    const cJSON* props = GetObject(widget_json, "properties");
    const std::string resolved_text = direct_text.empty() ? ResolveText(props, texts) : direct_text;

    if (auto* text_widget = dynamic_cast<TextWidget*>(widget.get())) {
        if (props) {
            const cJSON* text_key = GetObject(props, "textId");
            if (!text_key) {
                text_key = GetObject(props, "text");
            }
            if (!text_key) {
                text_key = GetObject(props, "TextID");
            }
            if (!text_key) {
                text_key = GetObject(props, "Text");
            }
            if (cJSON_IsString(text_key) && text_key->valuestring) {
                text_widget->SetTextId(HashTextId(text_key->valuestring));
            }
        }
        if (!resolved_text.empty()) {
            text_widget->SetText(resolved_text);
        }
    }

    if (auto* checkbox = dynamic_cast<CheckboxWidget*>(widget.get())) {
        if (!GetObject(widget_json, "checked") || cJSON_IsBool(GetObject(widget_json, "checked"))) {
            checkbox->SetChecked(GetBool(widget_json, "checked", false));
        }
    }
    if (auto* radio = dynamic_cast<RadioWidget*>(widget.get())) {
        if (!GetObject(widget_json, "checked") || cJSON_IsBool(GetObject(widget_json, "checked"))) {
            radio->SetChecked(GetBool(widget_json, "checked", false));
        }
    }
    if (auto* sw = dynamic_cast<SwitchWidget*>(widget.get())) {
        if (!GetObject(widget_json, "checked") || cJSON_IsBool(GetObject(widget_json, "checked"))) {
            sw->SetChecked(GetBool(widget_json, "checked", false));
        }
    }
    if (auto* progress = dynamic_cast<ProgressWidget*>(widget.get())) {
        if (!GetObject(widget_json, "value") || cJSON_IsNumber(GetObject(widget_json, "value"))) {
            const int value = GetInt(widget_json, "value", 0);
            progress->SetValue(static_cast<uint8_t>(value));
        }
    }

    const cJSON* children = GetObject(widget_json, "children");
    if (children && cJSON_IsArray(children)) {
        if (!widgets_json) {
            return nullptr;
        }
        if (!IsContainerType(type)) {
            return nullptr;
        }
        const cJSON* child_id = nullptr;
        cJSON_ArrayForEach(child_id, children) {
            if (!cJSON_IsString(child_id) || !child_id->valuestring) {
                return nullptr;
            }
            const std::string child_key = child_id->valuestring;
            if (stack.find(child_key) != stack.end()) {
                return nullptr;
            }
            const cJSON* child_json = cJSON_GetObjectItemCaseSensitive(widgets_json, child_key.c_str());
            if (!cJSON_IsObject(child_json)) {
                return nullptr;
            }
            stack.insert(child_key);
            auto child = BuildWidgetRecursive(widgets_json, child_json, texts, stack);
            stack.erase(child_key);
            if (!child) {
                return nullptr;
            }
            widget->AddChild(std::move(child));
        }
    }

    return widget;
}

bool DefaultFocusable(std::string_view type) {
    return type == "button" || type == "checkbox" || type == "radio" || type == "switch" ||
           type == "textarea" || type == "listview";
}

std::unique_ptr<Widget> CreateWidgetByType(const std::string& type) {
    static const std::unordered_map<std::string, WidgetFactoryFn> kWidgetFactory = {
        {"label", [] { return std::make_unique<LabelWidget>(); }},
        {"button", [] { return std::make_unique<ButtonWidget>(); }},
        {"checkbox", [] { return std::make_unique<CheckboxWidget>(); }},
        {"radio", [] { return std::make_unique<RadioWidget>(); }},
        {"switch", [] { return std::make_unique<SwitchWidget>(); }},
        {"progress", [] { return std::make_unique<ProgressWidget>(); }},
        {"image", [] { return std::make_unique<ImageWidget>(); }},
        {"container", [] { return std::make_unique<ContainerWidget>(); }},
        {"textarea", [] { return std::make_unique<TextAreaWidget>(); }},
        {"listview", [] { return std::make_unique<ListViewWidget>(); }},
        {"frame", [] { return std::make_unique<FrameWidget>(); }},
        {"tabview", [] { return std::make_unique<TabViewWidget>(); }},
        {"menu", [] { return std::make_unique<MenuWidget>(); }},
        {"dialog", [] { return std::make_unique<DialogWidget>(); }},
        {"softkeyboard", [] { return std::make_unique<SoftKeyboardWidget>(); }},
        {"topbar", [] { return std::make_unique<TopBarWidget>(); }},
        {"bottombar", [] { return std::make_unique<BottomBarWidget>(); }},
    };

    auto it = kWidgetFactory.find(type);
    if (it != kWidgetFactory.end()) {
        return it->second();
    }
    return nullptr;
}

std::unique_ptr<Widget> CreateWidgetByType(WidgetType type) {
    switch (type) {
        case WidgetType::Label:
            return std::make_unique<LabelWidget>();
        case WidgetType::Image:
            return std::make_unique<ImageWidget>();
        case WidgetType::Button:
            return std::make_unique<ButtonWidget>();
        case WidgetType::Checkbox:
            return std::make_unique<CheckboxWidget>();
        case WidgetType::Radio:
            return std::make_unique<RadioWidget>();
        case WidgetType::Switch:
            return std::make_unique<SwitchWidget>();
        case WidgetType::Progress:
            return std::make_unique<ProgressWidget>();
        case WidgetType::TextArea:
            return std::make_unique<TextAreaWidget>();
        case WidgetType::ListView:
            return std::make_unique<ListViewWidget>();
        case WidgetType::TabView:
            return std::make_unique<TabViewWidget>();
        case WidgetType::Frame:
            return std::make_unique<FrameWidget>();
        case WidgetType::Menu:
            return std::make_unique<MenuWidget>();
        case WidgetType::Dialog:
            return std::make_unique<DialogWidget>();
        case WidgetType::SoftKeyboard:
            return std::make_unique<SoftKeyboardWidget>();
        case WidgetType::TopBar:
            return std::make_unique<TopBarWidget>();
        case WidgetType::BottomBar:
            return std::make_unique<BottomBarWidget>();
        default:
            return std::make_unique<ContainerWidget>();
    }
}

} // namespace

std::unique_ptr<Widget> WidgetBuilder::BuildWidget(const cJSON* widget_json, const cJSON* texts) {
    if (!widget_json || !cJSON_IsObject(widget_json)) {
        return nullptr;
    }
    std::unordered_set<std::string> stack;
    return BuildWidgetRecursive(nullptr, widget_json, texts, stack);
}

std::unique_ptr<Widget> WidgetBuilder::BuildScene(const cJSON* scene_json,
                                                  const cJSON* widgets_json,
                                                  const cJSON* texts) {
    if (!scene_json || !widgets_json || !cJSON_IsObject(scene_json) || !cJSON_IsObject(widgets_json)) {
        return nullptr;
    }
    const cJSON* root_id = GetObject(scene_json, "root");
    if (!cJSON_IsString(root_id) || !root_id->valuestring) {
        return nullptr;
    }
    const cJSON* root_json = cJSON_GetObjectItemCaseSensitive(widgets_json, root_id->valuestring);
    if (!cJSON_IsObject(root_json)) {
        return nullptr;
    }
    std::unordered_set<std::string> stack;
    stack.insert(root_id->valuestring);
    return BuildWidgetRecursive(widgets_json, root_json, texts, stack);
}

std::unique_ptr<Widget> BuildWidgetTree(const resource::WidgetInit* inits,
                                        size_t count,
                                        uint32_t root_id) {
    if (!inits || count == 0 || root_id == 0) {
        return nullptr;
    }

    std::unordered_map<uint32_t, std::unique_ptr<Widget>> owned;
    owned.reserve(count);

    std::unordered_map<uint32_t, Widget*> raw;
    raw.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        const auto& init = inits[i];
        auto widget = CreateWidgetByType(init.type);
        if (!widget) {
            continue;
        }
        widget->SetRectInParent(init.rect);
        auto* raw_ptr = widget.get();
        owned.emplace(init.id, std::move(widget));
        raw.emplace(init.id, raw_ptr);
    }

    auto root_it = owned.find(root_id);
    if (root_it == owned.end()) {
        return nullptr;
    }

    std::unique_ptr<Widget> root = std::move(root_it->second);
    owned.erase(root_it);
    raw[root_id] = root.get();

    for (size_t i = 0; i < count; ++i) {
        const auto& init = inits[i];
        if (init.id == root_id) {
            continue;
        }
        auto child_it = owned.find(init.id);
        if (child_it == owned.end()) {
            continue;
        }
        const uint32_t parent_id = init.parent_id;
        auto parent_it = raw.find(parent_id);
        if (parent_it == raw.end()) {
            continue;
        }
        parent_it->second->AddChild(std::move(child_it->second));
        owned.erase(child_it);
    }

    return root;
}

} // namespace app_ui
