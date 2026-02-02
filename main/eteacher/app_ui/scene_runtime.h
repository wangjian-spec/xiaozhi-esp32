#pragma once

#include <memory>
#include <string>

#include "focus_manager.h"
#include "widget.h"

struct cJSON;

namespace app_ui {

struct SceneRuntime {
    uint16_t scene_id = 0;
    std::unique_ptr<Widget> root;
    FocusManager focus;
};

namespace runtime {

class SceneManager {
public:
    bool LoadFromJson(const cJSON* root, const char* scene_id, uint16_t scene_index);

    Widget* Root() const;
    FocusManager& Focus();
    uint16_t SceneId() const;

private:
    SceneRuntime scene_{};
};

} // namespace runtime

} // namespace app_ui
