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

bool DefaultFocusable(WidgetType type);
bool IsContainerType(WidgetType type);
WidgetType ParseWidgetType(std::string_view type);
std::unique_ptr<Widget> CreateWidgetByType(WidgetType type);
void ApplyWidgetCommon(Widget* widget, const desc::WidgetDesc& desc);
void ApplyWidgetSpecific(Widget* widget, const desc::WidgetDesc& desc);

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

bool GetBool(const cJSON* obj, const char* key, bool fallback) {
    const cJSON* item = GetObject(obj, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
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
    const std::string type_lower = ToLower(type_raw);
    const WidgetType type = ParseWidgetType(type_lower);
    if (type == WidgetType::Unknown) {
        printf("[WidgetBuilder] Unknown widget type: %s\n", type_raw.c_str());
        return nullptr;
    }

    auto widget = CreateWidgetByType(type);
    if (!widget) {
        return nullptr;
    }

    const cJSON* rect_json = GetObject(widget_json, "rect");
    if (!cJSON_IsObject(rect_json)) {
        printf("[WidgetBuilder] Widget rect missing or invalid\n");
        return nullptr;
    }

    const cJSON* x_item = GetObject(rect_json, "x");
    const cJSON* y_item = GetObject(rect_json, "y");
    const cJSON* w_item = GetObject(rect_json, "w");
    const cJSON* h_item = GetObject(rect_json, "h");
    if (!cJSON_IsNumber(x_item) || !cJSON_IsNumber(y_item) ||
        !cJSON_IsNumber(w_item) || !cJSON_IsNumber(h_item)) {
        printf("[WidgetBuilder] Widget rect missing x/y/w/h\n");
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
        printf("[WidgetBuilder] Widget visible must be bool\n");
        return nullptr;
    }
    const cJSON* enabled_item = GetObject(widget_json, "enabled");
    if (enabled_item && !cJSON_IsBool(enabled_item)) {
        printf("[WidgetBuilder] Widget enabled must be bool\n");
        return nullptr;
    }
    const cJSON* focusable_item = GetObject(widget_json, "focusable");
    if (focusable_item && !cJSON_IsBool(focusable_item)) {
        printf("[WidgetBuilder] Widget focusable must be bool\n");
        return nullptr;
    }

    const bool visible = GetBool(widget_json, "visible", true);
    const bool enabled = GetBool(widget_json, "enabled", true);
    const bool focusable = GetBool(widget_json, "focusable", DefaultFocusable(type));

    widget->SetVisible(visible);
    widget->SetEnabled(enabled);
    widget->SetFocusable(focusable);

    widget->InitFromJson(widget_json, texts);

    const cJSON* children = GetObject(widget_json, "children");
    if (children && cJSON_IsArray(children)) {
        if (!widgets_json) {
            printf("[WidgetBuilder] Widget children missing widgets table\n");
            return nullptr;
        }
        if (!IsContainerType(type)) {
            printf("[WidgetBuilder] Widget is not container but has children\n");
            return nullptr;
        }
        const cJSON* child_id = nullptr;
        cJSON_ArrayForEach(child_id, children) {
            if (!cJSON_IsString(child_id) || !child_id->valuestring) {
                printf("[WidgetBuilder] Skip child: invalid id\n");
                continue;
            }
            const std::string child_key = child_id->valuestring;
            if (stack.find(child_key) != stack.end()) {
                printf("[WidgetBuilder] Skip child: cycle detected (%s)\n", child_key.c_str());
                continue;
            }
            const cJSON* child_json = cJSON_GetObjectItemCaseSensitive(widgets_json, child_key.c_str());
            if (!cJSON_IsObject(child_json)) {
                printf("[WidgetBuilder] Skip child: not found (%s)\n", child_key.c_str());
                continue;
            }
            stack.insert(child_key);
            auto child = BuildWidgetRecursive(widgets_json, child_json, texts, stack);
            stack.erase(child_key);
            if (!child) {
                printf("[WidgetBuilder] Skip child: build failed (%s)\n", child_key.c_str());
                continue;
            }
            widget->AddChild(std::move(child));
        }
    }

    return widget;
}

bool DefaultFocusable(WidgetType type) {
    return type == WidgetType::Button || type == WidgetType::Checkbox || type == WidgetType::Radio ||
           type == WidgetType::Switch || type == WidgetType::TextArea || type == WidgetType::ListView;
}

bool IsContainerType(WidgetType type) {
    return type == WidgetType::Container || type == WidgetType::Frame || type == WidgetType::Menu ||
           type == WidgetType::ListView || type == WidgetType::TabView || type == WidgetType::Dialog ||
           type == WidgetType::SoftKeyboard || type == WidgetType::TopBar || type == WidgetType::BottomBar;
}

WidgetType ParseWidgetType(std::string_view type) {
    static const std::unordered_map<std::string_view, WidgetType> kTypeMap = {
        {"label", WidgetType::Label},
        {"image", WidgetType::Image},
        {"button", WidgetType::Button},
        {"checkbox", WidgetType::Checkbox},
        {"radio", WidgetType::Radio},
        {"switch", WidgetType::Switch},
        {"progress", WidgetType::Progress},
        {"textarea", WidgetType::TextArea},
        {"listview", WidgetType::ListView},
        {"tabview", WidgetType::TabView},
        {"frame", WidgetType::Frame},
        {"menu", WidgetType::Menu},
        {"dialog", WidgetType::Dialog},
        {"softkeyboard", WidgetType::SoftKeyboard},
        {"topbar", WidgetType::TopBar},
        {"bottombar", WidgetType::BottomBar},
        {"container", WidgetType::Container},
    };
    auto it = kTypeMap.find(type);
    if (it != kTypeMap.end()) {
        return it->second;
    }
    return WidgetType::Unknown;
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
        case WidgetType::Container:
            return std::make_unique<ContainerWidget>();
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
            printf("[WidgetBuilder] Unknown widget type enum: %u\n", static_cast<unsigned>(type));
            return nullptr;
    }
}

void ApplyWidgetCommon(Widget* widget, const desc::WidgetDesc& desc) {
    if (!widget) {
        return;
    }
    widget->SetRectInParent(desc.rect);
    widget->SetVisible((desc.flags & desc::kWidgetFlagVisible) != 0);
    widget->SetEnabled((desc.flags & desc::kWidgetFlagEnabled) != 0);
    widget->SetFocusable((desc.flags & desc::kWidgetFlagFocusable) != 0);

    auto* basic = static_cast<BasicWidget*>(widget);
    basic->ApplyStyle(static_cast<uint16_t>(desc.style_id));
}

void ApplyTextDesc(TextWidget* widget, const desc::TextDesc* desc) {
    if (!widget || !desc) {
        return;
    }
    if (desc->text_id != 0) {
        widget->SetTextId(desc->text_id);
    }
    if (desc->text && desc->text[0]) {
        widget->SetText(desc->text);
    }
}

void ApplyCheckableDesc(TextWidget* widget, const desc::CheckableDesc* desc, WidgetType type) {
    if (!widget || !desc) {
        return;
    }
    if (desc->text_id != 0) {
        widget->SetTextId(desc->text_id);
    }
    if (desc->text && desc->text[0]) {
        widget->SetText(desc->text);
    }
    switch (type) {
        case WidgetType::Checkbox:
            static_cast<CheckboxWidget*>(widget)->SetChecked(desc->checked);
            break;
        case WidgetType::Radio:
            static_cast<RadioWidget*>(widget)->SetChecked(desc->checked);
            break;
        case WidgetType::Switch:
            static_cast<SwitchWidget*>(widget)->SetChecked(desc->checked);
            break;
        default:
            break;
    }
}

void ApplyProgressDesc(TextWidget* widget, const desc::ProgressDesc* desc) {
    if (!widget || !desc) {
        return;
    }
    if (desc->text_id != 0) {
        widget->SetTextId(desc->text_id);
    }
    if (desc->text && desc->text[0]) {
        widget->SetText(desc->text);
    }
    static_cast<ProgressWidget*>(widget)->SetValue(desc->value);
}

void ApplyWidgetSpecific(Widget* widget, const desc::WidgetDesc& desc) {
    if (!widget || !desc.specific) {
        return;
    }
    switch (desc.type) {
        case WidgetType::Label:
        case WidgetType::Image:
        case WidgetType::Button:
        case WidgetType::TextArea:
        case WidgetType::ListView:
        case WidgetType::TabView:
        case WidgetType::Frame:
        case WidgetType::Menu:
        case WidgetType::Dialog:
        case WidgetType::TopBar:
        case WidgetType::BottomBar:
            ApplyTextDesc(static_cast<TextWidget*>(widget),
                          static_cast<const desc::TextDesc*>(desc.specific));
            break;
        case WidgetType::Checkbox:
        case WidgetType::Radio:
        case WidgetType::Switch:
            ApplyCheckableDesc(static_cast<TextWidget*>(widget),
                               static_cast<const desc::CheckableDesc*>(desc.specific),
                               desc.type);
            break;
        case WidgetType::Progress:
            ApplyProgressDesc(static_cast<TextWidget*>(widget),
                              static_cast<const desc::ProgressDesc*>(desc.specific));
            break;
        default:
            break;
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

std::unique_ptr<Widget> BuildWidgetTree(const desc::WidgetDesc* widgets,
                                        size_t count,
                                        uint32_t root_id) {
    if (!widgets || count == 0 || root_id == 0) {
        return nullptr;
    }

    std::unordered_map<uint32_t, std::unique_ptr<Widget>> owned;
    owned.reserve(count);

    std::unordered_map<uint32_t, Widget*> raw;
    raw.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        const auto& desc = widgets[i];
        auto widget = CreateWidgetByType(desc.type);
        if (!widget) {
            continue;
        }
        ApplyWidgetCommon(widget.get(), desc);
        ApplyWidgetSpecific(widget.get(), desc);
        auto* raw_ptr = widget.get();
        owned.emplace(desc.id, std::move(widget));
        raw.emplace(desc.id, raw_ptr);
    }

    auto root_it = owned.find(root_id);
    if (root_it == owned.end()) {
        return nullptr;
    }

    std::unique_ptr<Widget> root = std::move(root_it->second);
    owned.erase(root_it);
    raw[root_id] = root.get();

    for (size_t i = 0; i < count; ++i) {
        const auto& desc = widgets[i];
        if (desc.id == root_id) {
            continue;
        }
        auto child_it = owned.find(desc.id);
        if (child_it == owned.end()) {
            continue;
        }
        const uint32_t parent_id = desc.parent_id;
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
