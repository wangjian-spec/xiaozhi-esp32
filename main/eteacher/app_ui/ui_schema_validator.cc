#include "ui_schema_validator.h"

#include <cJSON.h>

#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <unordered_set>

namespace app_ui {

namespace {

const cJSON* GetObject(const cJSON* obj, const char* key) {
    return obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : nullptr;
}

bool IsContainerType(std::string_view type) {
    return type == "container" || type == "panel" || type == "frame" || type == "groupbox" ||
           type == "hbox" || type == "vbox" || type == "gridlayout" || type == "menu" ||
           type == "menubar" || type == "submenu" || type == "contextmenu" ||
           type == "navbar" || type == "sidemenu" || type == "drawer" ||
           type == "tabview" || type == "tabwidget" || type == "tabpage" ||
           type == "dialog" || type == "confirmdialog" || type == "alert" ||
           type == "toast" || type == "popover" || type == "modal" || type == "overlay";
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
        error = std::string("Widget reused across scenes: ") + widget_id;
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
    const cJSON* scenes = GetObject(root, "scenes");
    const cJSON* widgets = GetObject(root, "widgets");
    if (!cJSON_IsObject(scenes) || !cJSON_IsObject(widgets)) {
        error = "Missing scenes/widgets";
        return false;
    }

    std::map<std::string, std::string> ownership;
    const cJSON* scene = nullptr;
    cJSON_ArrayForEach(scene, scenes) {
        if (!scene || !scene->string || !cJSON_IsObject(scene)) {
            error = "Scene entry invalid";
            return false;
        }
        const cJSON* root_id = GetObject(scene, "root");
        if (!cJSON_IsString(root_id) || !root_id->valuestring) {
            error = std::string("Scene missing root: ") + scene->string;
            return false;
        }
        const cJSON* root_widget = cJSON_GetObjectItemCaseSensitive(widgets, root_id->valuestring);
        if (!cJSON_IsObject(root_widget)) {
            error = std::string("Scene root widget not found: ") + root_id->valuestring;
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
            error = std::string("Scene root must be container: ") + root_id->valuestring;
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

    return true;
}

} // namespace app_ui
