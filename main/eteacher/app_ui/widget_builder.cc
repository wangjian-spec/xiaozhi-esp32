// Widget 构建器实现
// 本文件实现从静态描述符（生产用）构建 Widget 树的逻辑。

#include "widget_builder.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "scene.h"
#include "widget.h"

namespace app_ui {

namespace {

std::unique_ptr<Widget> CreateWidgetByType(WidgetType type);
void ApplyWidgetCommon(Widget* widget, const desc::WidgetDesc& desc);
void ApplyWidgetSpecific(Widget* widget, const desc::WidgetDesc& desc);

struct Node {
    uint32_t id = 0;
    uint32_t parent = 0;
    std::unique_ptr<Widget> widget;
    std::vector<size_t> children;
};

size_t FindNodeIndex(const std::vector<Node>& nodes, uint32_t id) {
    auto it = std::lower_bound(nodes.begin(), nodes.end(), id, [](const Node& node, uint32_t value) {
        return node.id < value;
    });
    if (it == nodes.end() || it->id != id) {
        return nodes.size();
    }
    return static_cast<size_t>(std::distance(nodes.begin(), it));
}

std::unique_ptr<Widget> BuildSubtree(size_t index, std::vector<Node>& nodes) {
    if (index >= nodes.size() || !nodes[index].widget) {
        return nullptr;
    }
    std::unique_ptr<Widget> root = std::move(nodes[index].widget);
    for (size_t child_index : nodes[index].children) {
        std::unique_ptr<Widget> child = BuildSubtree(child_index, nodes);
        if (child) {
            root->AddChild(std::move(child));
        }
    }
    return root;
}

std::unique_ptr<Widget> CreateWidgetByType(WidgetType type) {
    // 通用：根据 WidgetType 创建对应的 Widget 实例。
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
    // 从静态描述符填充通用属性（静态构建路径使用）
    widget->SetId(desc.id);
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
    // 将静态文本描述应用到 TextWidget（静态描述符路径）
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
    // 将可选中控件的静态描述应用到 widget（静态描述符路径）
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
    // 将进度控件的静态描述应用到 widget（静态描述符路径）
    if (desc->text_id != 0) {
        widget->SetTextId(desc->text_id);
    }
    if (desc->text && desc->text[0]) {
        widget->SetText(desc->text);
    }
    static_cast<ProgressWidget*>(widget)->SetValue(desc->value);
}

// 静态描述符填充函数（用于生产路径，从 desc::WidgetDesc 填充 Widget 特定字段）
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

// 静态/资源构建接口（生产用）。
// 这些函数使用编译期生成的描述符或资源初始化表构建 Widget 树。
//
// 下面两个重载的区别：
// - `BuildWidgetTree(const resource::WidgetInit*)`：接受更简单的资源初始化表，通常由资源打包器
//   或工具链生成，包含每个控件的 `id`/`parent_id`/`type`/`rect` 等基本信息，适用于资源驱动的轻量初始化。
// - `BuildWidgetTree(const desc::WidgetDesc*)`：接受 richer 的静态描述符，包含样式 id、特定控件的详细描述（`specific`）、
//   flags 等，更适合完整 UI 描述用于生产路径。
//
// 两个接口共存是为了兼容不同的生成/打包流程，调用者根据编译时配置选择合适的数据源，
// 而实现内部复用相似的构建流程以生成 `Widget` 树。
std::unique_ptr<Widget> BuildWidgetTree(const resource::WidgetInit* inits,
                                        size_t count,
                                        uint32_t root_id) {
    if (!inits || count == 0 || root_id == 0) {
        return nullptr;
    }

    std::vector<Node> nodes;
    nodes.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        const auto& init = inits[i];
        auto widget = CreateWidgetByType(init.type);
        if (!widget) {
            continue;
        }
        widget->SetId(init.id);
        widget->SetRectInParent(init.rect);
        nodes.push_back({init.id, init.parent_id, std::move(widget), {}});
    }
    if (nodes.empty()) {
        return nullptr;
    }
    std::sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
        return a.id < b.id;
    });
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].id == root_id) {
            continue;
        }
        const size_t parent_index = FindNodeIndex(nodes, nodes[i].parent);
        if (parent_index == nodes.size()) {
            continue;
        }
        nodes[parent_index].children.push_back(i);
    }
    const size_t root_index = FindNodeIndex(nodes, root_id);
    if (root_index == nodes.size()) {
        return nullptr;
    }
    return BuildSubtree(root_index, nodes);
}

std::unique_ptr<Widget> BuildWidgetTree(const desc::WidgetDesc* widgets,
                                        size_t count,
                                        uint32_t root_id) {
    // 从 `desc::WidgetDesc` 静态描述符构建 Widget 树（生产路径）
    // - `widgets`：静态描述符数组，包含控件类型、rect、parent_id、flags、style_id、specific 指针等信息。
    // - `count`：描述符数量。
    // - `root_id`：根控件 id（必须与描述符中的 id 对应）。
    // 返回：拥有根 `Widget` 的 `std::unique_ptr<Widget>`，失败返回 `nullptr`。

    // 注意：此处为注释说明，不改变现有构建逻辑。
    if (!widgets || count == 0 || root_id == 0) {
        return nullptr;
    }

    std::vector<Node> nodes;
    nodes.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        const auto& desc = widgets[i];
        auto widget = CreateWidgetByType(desc.type);
        if (!widget) {
            continue;
        }
        ApplyWidgetCommon(widget.get(), desc);
        ApplyWidgetSpecific(widget.get(), desc);
        nodes.push_back({desc.id, desc.parent_id, std::move(widget), {}});
    }
    if (nodes.empty()) {
        return nullptr;
    }
    std::sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
        return a.id < b.id;
    });
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].id == root_id) {
            continue;
        }
        const size_t parent_index = FindNodeIndex(nodes, nodes[i].parent);
        if (parent_index == nodes.size()) {
            continue;
        }
        nodes[parent_index].children.push_back(i);
    }
    const size_t root_index = FindNodeIndex(nodes, root_id);
    if (root_index == nodes.size()) {
        return nullptr;
    }
    return BuildSubtree(root_index, nodes);
}

} // namespace app_ui
