#include "scene.h"

#include <cJSON.h>

#include <cctype>
#include <cstdio>
#include <map>
#include <string_view>
#include <unordered_set>

#include "widget.h"
#include "widget_builder.h"

namespace app_ui {

void SceneManager::Push(std::unique_ptr<Scene> scene) {
    if (!scene) {
        return;
    }
    if (!stack_.empty()) {
        stack_.back()->OnPause();
    }
    scene->OnEnter();
    stack_.push_back(std::move(scene));
}

void SceneManager::Pop() {
    if (stack_.empty()) {
        return;
    }
    stack_.back()->OnExit();
    stack_.pop_back();
    if (!stack_.empty()) {
        stack_.back()->OnResume();
    }
}

void SceneManager::Replace(std::unique_ptr<Scene> scene) {
    Pop();
    Push(std::move(scene));
}

Scene* SceneManager::FindByName(const std::string& name) {
    for (const auto& scene : stack_) {
        if (scene && name == scene->Name()) {
            return scene.get();
        }
    }
    return nullptr;
}

Scene* SceneManager::Current() {
    if (stack_.empty()) {
        return nullptr;
    }
    return stack_.back().get();
}

void SceneManager::Clear() {
    while (!stack_.empty()) {
        Pop();
    }
}

bool SceneManager::PromoteToTop(const std::string& name) {
    if (stack_.empty()) {
        return false;
    }
    size_t index = stack_.size();
    for (size_t i = 0; i < stack_.size(); ++i) {
        if (stack_[i] && name == stack_[i]->Name()) {
            index = i;
            break;
        }
    }
    if (index >= stack_.size()) {
        return false;
    }
    if (index == stack_.size() - 1) {
        return true;
    }
    if (!stack_.empty()) {
        stack_.back()->OnPause();
    }
    auto scene = std::move(stack_[index]);
    stack_.erase(stack_.begin() + static_cast<long>(index));
    scene->OnResume();
    stack_.push_back(std::move(scene));
    return true;
}

} // namespace app_ui

namespace app_ui {

namespace {

const cJSON* GetObject(const cJSON* obj, const char* key) {
    return obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : nullptr;
}

bool IsContainerType(std::string_view type) {
    return type == "container" || type == "frame" || type == "menu" || type == "listview" ||
           type == "tabview" || type == "dialog" || type == "softkeyboard" || type == "topbar" ||
           type == "bottombar";
}

std::string ToLower(const char* str) {
    std::string out = str ? str : "";
    for (auto& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool ValidateWidget(const cJSON* widgets, const cJSON* widget, std::string& error) {
    if (!cJSON_IsObject(widget)) {
        error = "Widget must be object";
        return false;
    }
    if (GetObject(widget, "parent")) {
        error = "Widget must not contain parent field";
        return false;
    }
    const cJSON* type = GetObject(widget, "type");
    if (!cJSON_IsString(type) || !type->valuestring || !type->valuestring[0]) {
        error = "Widget missing type";
        return false;
    }
    const cJSON* rect = GetObject(widget, "rect");
    if (!cJSON_IsObject(rect)) {
        error = "Widget missing rect";
        return false;
    }
    if (!cJSON_IsNumber(GetObject(rect, "x")) || !cJSON_IsNumber(GetObject(rect, "y")) ||
        !cJSON_IsNumber(GetObject(rect, "w")) || !cJSON_IsNumber(GetObject(rect, "h"))) {
        error = "Widget rect must have x/y/w/h";
        return false;
    }
    const cJSON* children = GetObject(widget, "children");
    if (children && !cJSON_IsArray(children)) {
        error = "Widget children must be array";
        return false;
    }
    const std::string type_lower = ToLower(type->valuestring);
    if (children && cJSON_GetArraySize(children) > 0 && !IsContainerType(type_lower)) {
        error = "Non-container widget has children";
        return false;
    }
    if (children) {
        const cJSON* child = nullptr;
        cJSON_ArrayForEach(child, children) {
            if (!cJSON_IsString(child) || !child->valuestring) {
                error = "Child id must be string";
                return false;
            }
            if (!cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(widgets, child->valuestring))) {
                error = std::string("Child widget not found: ") + child->valuestring;
                return false;
            }
        }
    }
    return true;
}

bool TraverseSceneTree(const cJSON* widgets,
                       const std::string& widget_id,
                       const char* scene_id,
                       std::map<std::string, std::string>& owner,
                       std::unordered_set<std::string>& visiting,
                       std::string& error) {
    if (!scene_id || widget_id.empty()) {
        error = "Invalid scene/widget id";
        return false;
    }

    auto owned = owner.find(widget_id);
    if (owned != owner.end() && owned->second != scene_id) {
        error = std::string("Widget reused across pages: ") + widget_id;
        return false;
    }
    if (visiting.find(widget_id) != visiting.end()) {
        error = std::string("Cycle detected at widget: ") + widget_id;
        return false;
    }

    const cJSON* widget = cJSON_GetObjectItemCaseSensitive(widgets, widget_id.c_str());
    if (!cJSON_IsObject(widget)) {
        error = std::string("Widget not found: ") + widget_id;
        return false;
    }

    owner.emplace(widget_id, scene_id);
    visiting.insert(widget_id);

    const cJSON* children = GetObject(widget, "children");
    if (children && cJSON_IsArray(children)) {
        const cJSON* child = nullptr;
        cJSON_ArrayForEach(child, children) {
            if (!cJSON_IsString(child) || !child->valuestring) {
                error = "Child id must be string";
                return false;
            }
            if (!TraverseSceneTree(widgets, child->valuestring, scene_id, owner, visiting, error)) {
                return false;
            }
        }
    }

    visiting.erase(widget_id);
    return true;
}

} // namespace

bool UiSchemaValidator::Validate(const cJSON* root, std::string& error) {
    if (!cJSON_IsObject(root)) {
        error = "Root must be object";
        return false;
    }
    const cJSON* scenes = GetObject(root, "pages");
    if (!cJSON_IsObject(scenes)) {
        scenes = GetObject(root, "scenes");
    }
    const cJSON* widgets = GetObject(root, "widgets");
    if (!cJSON_IsObject(scenes) || !cJSON_IsObject(widgets)) {
        error = "Missing pages/widgets";
        return false;
    }

    std::map<std::string, std::string> ownership;
    const cJSON* scene = nullptr;
    cJSON_ArrayForEach(scene, scenes) {
        if (!scene || !scene->string || !cJSON_IsObject(scene)) {
            error = "Page entry invalid";
            return false;
        }
        const cJSON* root_id = GetObject(scene, "root");
        if (!cJSON_IsString(root_id) || !root_id->valuestring) {
            error = std::string("Page missing root: ") + scene->string;
            return false;
        }
        const cJSON* root_widget = cJSON_GetObjectItemCaseSensitive(widgets, root_id->valuestring);
        if (!cJSON_IsObject(root_widget)) {
            error = std::string("Page root widget not found: ") + root_id->valuestring;
            return false;
        }
        if (!ValidateWidget(widgets, root_widget, error)) {
            return false;
        }

        const cJSON* type = GetObject(root_widget, "type");
        const std::string type_lower = (cJSON_IsString(type) && type->valuestring)
                                             ? ToLower(type->valuestring)
                                             : std::string();
        if (!IsContainerType(type_lower)) {
            error = std::string("Page root must be container: ") + root_id->valuestring;
            return false;
        }

        std::unordered_set<std::string> visiting;
        if (!TraverseSceneTree(widgets, root_id->valuestring, scene->string, ownership, visiting, error)) {
            return false;
        }
    }

    const cJSON* widget = nullptr;
    cJSON_ArrayForEach(widget, widgets) {
        if (!widget || !widget->string) {
            error = "Widget id missing";
            return false;
        }
        if (!ValidateWidget(widgets, widget, error)) {
            error = std::string("Widget invalid: ") + widget->string + " -> " + error;
            return false;
        }
    }

    const cJSON* public_section = GetObject(root, "public");
    if (public_section) {
        if (!cJSON_IsObject(public_section)) {
            error = "Public section must be object";
            return false;
        }
        const cJSON* public_page = GetObject(public_section, "page");
        const cJSON* public_widgets = GetObject(public_section, "widgets");
        if (!cJSON_IsObject(public_page) || !cJSON_IsObject(public_widgets)) {
            error = "Public section missing page/widgets";
            return false;
        }
        const cJSON* public_root_id = GetObject(public_page, "root");
        if (!cJSON_IsString(public_root_id) || !public_root_id->valuestring) {
            error = "Public page missing root";
            return false;
        }
        const cJSON* public_root_widget =
            cJSON_GetObjectItemCaseSensitive(public_widgets, public_root_id->valuestring);
        if (!cJSON_IsObject(public_root_widget)) {
            error = std::string("Public root widget not found: ") + public_root_id->valuestring;
            return false;
        }
        if (!ValidateWidget(public_widgets, public_root_widget, error)) {
            return false;
        }
        const cJSON* type = GetObject(public_root_widget, "type");
        const std::string type_lower = (cJSON_IsString(type) && type->valuestring)
                                             ? ToLower(type->valuestring)
                                             : std::string();
        if (!IsContainerType(type_lower)) {
            error = std::string("Public root must be container: ") + public_root_id->valuestring;
            return false;
        }

        std::map<std::string, std::string> public_ownership;
        std::unordered_set<std::string> public_visiting;
        if (!TraverseSceneTree(public_widgets, public_root_id->valuestring, "public",
                               public_ownership, public_visiting, error)) {
            return false;
        }

        const cJSON* public_widget = nullptr;
        cJSON_ArrayForEach(public_widget, public_widgets) {
            if (!public_widget || !public_widget->string) {
                error = "Public widget id missing";
                return false;
            }
            if (cJSON_GetObjectItemCaseSensitive(widgets, public_widget->string)) {
                error = std::string("Public widget id conflicts with page widget: ") +
                        public_widget->string;
                return false;
            }
            if (!ValidateWidget(public_widgets, public_widget, error)) {
                error = std::string("Public widget invalid: ") + public_widget->string +
                        " -> " + error;
                return false;
            }
        }
    }

    return true;
}

namespace runtime {

bool SceneManager::LoadFromJson(const cJSON* root, const char* scene_id, uint16_t scene_index) {
    if (!root || !scene_id) {
        printf("[SceneRuntime] invalid args: root=%p scene_id=%p\n", root, scene_id);
        return false;
    }
    std::string error;
    if (!UiSchemaValidator::Validate(root, error)) {
        printf("[SceneRuntime] schema validate failed: %s\n", error.c_str());
        return false;
    }
    const cJSON* scenes = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (!cJSON_IsObject(scenes)) {
        scenes = cJSON_GetObjectItemCaseSensitive(root, "scenes");
    }
    const cJSON* widgets = cJSON_GetObjectItemCaseSensitive(root, "widgets");
    const cJSON* resources = cJSON_GetObjectItemCaseSensitive(root, "resources");
    const cJSON* texts = resources ? cJSON_GetObjectItemCaseSensitive(resources, "texts") : nullptr;
    if (!cJSON_IsObject(scenes) || !cJSON_IsObject(widgets)) {
        printf("[SceneRuntime] missing pages/widgets object\n");
        return false;
    }
    const cJSON* scene_json = cJSON_GetObjectItemCaseSensitive(scenes, scene_id);
    if (!cJSON_IsObject(scene_json)) {
        printf("[SceneRuntime] page not found: %s\n", scene_id);
        return false;
    }

    auto root_widget = WidgetBuilder::BuildScene(scene_json, widgets, texts);
    if (!root_widget) {
        printf("[SceneRuntime] BuildScene failed for page: %s\n", scene_id);
        return false;
    }

    std::unique_ptr<Widget> public_root;
    const cJSON* public_section = cJSON_GetObjectItemCaseSensitive(root, "public");
    if (cJSON_IsObject(public_section)) {
        const cJSON* public_page = cJSON_GetObjectItemCaseSensitive(public_section, "page");
        const cJSON* public_widgets = cJSON_GetObjectItemCaseSensitive(public_section, "widgets");
        if (cJSON_IsObject(public_page) && cJSON_IsObject(public_widgets)) {
            public_root = WidgetBuilder::BuildScene(public_page, public_widgets, texts);
        }
    }

    if (public_root) {
        Rect root_rect = root_widget->RectInParent();
        auto container_root = std::make_unique<ContainerWidget>();
        container_root->SetRectInParent(root_rect);
        container_root->AddChild(std::move(root_widget));
        container_root->AddChild(std::move(public_root));
        root_widget = std::move(container_root);
    }

    scene_.scene_id = scene_index;
    scene_.root = std::shared_ptr<Widget>(root_widget.release());
    scene_.focus.Build(scene_.root.get());
    return true;
}

Widget* SceneManager::Root() const {
    return scene_.root.get();
}

std::shared_ptr<Widget> SceneManager::RootShared() const {
    return scene_.root;
}

FocusManager& SceneManager::Focus() {
    return scene_.focus;
}

uint16_t SceneManager::SceneId() const {
    return scene_.scene_id;
}

} // namespace runtime

} // namespace app_ui
