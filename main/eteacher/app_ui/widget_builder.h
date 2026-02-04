#pragma once

#include <cstddef>
#include <memory>

#include "types.h"

struct cJSON;

namespace app_ui {

class Widget;

std::unique_ptr<Widget> BuildWidgetTree(const resource::WidgetInit* inits,
                                        size_t count,
                                        uint32_t root_id);

class WidgetBuilder {
public:
    static std::unique_ptr<Widget> BuildWidget(const cJSON* widget_json, const cJSON* texts);
    static std::unique_ptr<Widget> BuildScene(const cJSON* scene_json,
                                              const cJSON* widgets_json,
                                              const cJSON* texts);
};

} // namespace app_ui
