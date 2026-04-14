// 场景/运行时加载实现
// 本文件仅实现静态描述符（UiDesc）路径，作为正式运行版本。

#include "scene.h"

#include <cstdio>
#include <string_view>

#include "widget.h"
#include "widget_builder.h"
#include "debug.h"

namespace app_ui {

namespace runtime {

// 从编译时生成的描述符构建场景（生产路径，使用静态 Widget 表）
bool SceneRuntime::LoadFromDesc(const desc::UiDesc& ui, const char* scene_id, uint16_t scene_index) {
    if (!scene_id || !scene_id[0] || !ui.scenes || ui.scene_count == 0) {
        if (app_ui::debug::UiDebugLoggingEnabled()) {
            printf("[SceneRuntime] invalid desc args: scene_id=%p\n", scene_id);
        }
        return false;
    }

    const desc::SceneDesc* target = nullptr;
    for (size_t i = 0; i < ui.scene_count; ++i) {
        const auto& scene = ui.scenes[i];
        if (scene.id && std::string_view(scene.id) == scene_id) {
            target = &scene;
            break;
        }
    }
    if (!target) {
        if (app_ui::debug::UiDebugLoggingEnabled()) {
            printf("[SceneRuntime] desc page not found: %s\n", scene_id);
        }
        return false;
    }

    auto root_widget = BuildWidgetTree(target->widgets, target->widget_count, target->root_id);
    if (!root_widget) {
        if (app_ui::debug::UiDebugLoggingEnabled()) {
            printf("[SceneRuntime] BuildScene(desc) failed for page: %s\n", scene_id);
        }
        return false;
    }

    std::unique_ptr<Widget> public_root;
    if (ui.public_scene && ui.public_scene->widgets && ui.public_scene->widget_count > 0) {
        public_root = BuildWidgetTree(ui.public_scene->widgets,
                                      ui.public_scene->widget_count,
                                      ui.public_scene->root_id);
    }

    if (public_root) {
        Rect root_rect = root_widget->DeclaredRect();
        auto container_root = std::make_unique<ContainerWidget>();
        container_root->SetRectInParent({0, 0, root_rect.w, root_rect.h});
        container_root->AddChild(std::move(root_widget));
        container_root->AddChild(std::move(public_root));
        root_widget = std::move(container_root);
    }

    scene_id_ = scene_index;
    root_ = std::move(root_widget);
    app_ui::debug::PrintSceneLoaded(scene_id, scene_id_, root_.get(), public_root != nullptr);
    return true;
}

Widget* SceneRuntime::Root() const {
    return root_.get();
}

std::unique_ptr<Widget> SceneRuntime::TakeRoot() {
    return std::move(root_);
}

uint16_t SceneRuntime::SceneId() const {
    return scene_id_;
}

} // namespace runtime

// 从静态描述符收集场景 ID（生成的 UiDesc，生产路径）
std::vector<std::string> CollectSceneIds(const desc::UiDesc& ui) {
    std::vector<std::string> ids;
    for (size_t i = 0; i < ui.scene_count; ++i) {
        const auto& scene = ui.scenes[i];
        if (scene.id) {
            ids.emplace_back(scene.id);
        }
    }
    return ids;
}

} // namespace app_ui
