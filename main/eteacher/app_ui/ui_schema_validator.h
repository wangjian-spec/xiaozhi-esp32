#pragma once

#include <string>

struct cJSON;

namespace app_ui {

class UiSchemaValidator {
public:
    static bool Validate(const cJSON* root, std::string& error);
};

} // namespace app_ui
