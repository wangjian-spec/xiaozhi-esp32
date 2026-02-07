#pragma once

#include <cstdint>

struct cJSON;

namespace app_ui {

::cJSON* LoadUiJson(const char* name);

} // namespace app_ui
