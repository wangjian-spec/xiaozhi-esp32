#include "scene_runtime.h"

#include <cJSON.h>

#include "ui_schema_validator.h"
#include "widget_builder.h"

namespace app_ui {
namespace runtime {

bool SceneManager::LoadFromJson(const cJSON* root, const char* scene_id, uint16_t scene_index) {
    if (!root || !scene_id) {
        return false;
    }
    std::string error;
    if (!UiSchemaValidator::Validate(root, error)) {
        return false;
    }
    const cJSON* scenes = cJSON_GetObjectItemCaseSensitive(root, "scenes");
    const cJSON* widgets = cJSON_GetObjectItemCaseSensitive(root, "widgets");
    const cJSON* resources = cJSON_GetObjectItemCaseSensitive(root, "resources");
    const cJSON* texts = resources ? cJSON_GetObjectItemCaseSensitive(resources, "texts") : nullptr;
    if (!cJSON_IsObject(scenes) || !cJSON_IsObject(widgets)) {
        return false;
    }
    const cJSON* scene_json = cJSON_GetObjectItemCaseSensitive(scenes, scene_id);
    if (!cJSON_IsObject(scene_json)) {
        return false;
    }

    auto root_widget = WidgetBuilder::BuildScene(scene_json, widgets, texts);
    if (!root_widget) {
        return false;
    }

    scene_.scene_id = scene_index;
    scene_.root = std::move(root_widget);
    scene_.focus.Build(scene_.root.get());
    return true;
}

Widget* SceneManager::Root() const {
    return scene_.root.get();
}

FocusManager& SceneManager::Focus() {
    return scene_.focus;
}

uint16_t SceneManager::SceneId() const {
    return scene_.scene_id;
}

} // namespace runtime
} // namespace app_ui
