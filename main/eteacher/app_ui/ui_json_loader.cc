#include "ui_json_loader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <cJSON.h>

namespace {

cJSON* ParseEmbedded(const uint8_t* begin, const uint8_t* end, const char* name) {
    if (!begin || !end || end <= begin) {
        printf("[UiJson] Embedded %s missing or empty (begin=%p end=%p)\n", name, begin, end);
        return nullptr;
    }

    size_t len = static_cast<size_t>(end - begin);
    const uint8_t* data = begin;

    // cJSON does not accept UTF-8 BOM; strip it when present.
    if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        data += 3;
        len -= 3;
    }

    if (len >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
        printf("[UiJson] Embedded %s looks like UTF-16; please save as UTF-8 without BOM\n", name);
        return nullptr;
    }

    std::string_view sv(reinterpret_cast<const char*>(data), len);
    cJSON* root = cJSON_ParseWithLength(sv.data(), len);
    if (!root) {
        printf("[UiJson] Failed to parse %s len=%zu (first 256 bytes):\n%.*s\n",
               name,
               len,
               static_cast<int>(std::min<size_t>(len, 256)),
               sv.data());
    }
    return root;
}

} // namespace

namespace app_ui {

cJSON* LoadUiJson(const char* name) {
    if (!name || !name[0]) {
        return nullptr;
    }

    if (std::strcmp(name, "dictionary") == 0) {
        extern const uint8_t _binary_dictionary_json_start[] asm("_binary_dictionary_json_start");
        extern const uint8_t _binary_dictionary_json_end[] asm("_binary_dictionary_json_end");
        if (auto* root = ParseEmbedded(_binary_dictionary_json_start,
                                       _binary_dictionary_json_end,
                                       "dictionary.json")) {
            return root;
        }

        extern const uint8_t _binary_eteacher_apps_jsons_ui_json_dictionary_json_start[] __attribute__((weak));
        extern const uint8_t _binary_eteacher_apps_jsons_ui_json_dictionary_json_end[] __attribute__((weak));
        return ParseEmbedded(_binary_eteacher_apps_jsons_ui_json_dictionary_json_start,
                             _binary_eteacher_apps_jsons_ui_json_dictionary_json_end,
                             "dictionary.json");
    }

    if (std::strcmp(name, "device_setting") == 0) {
        extern const uint8_t _binary_device_setting_json_start[] asm("_binary_device_setting_json_start");
        extern const uint8_t _binary_device_setting_json_end[] asm("_binary_device_setting_json_end");
        if (auto* root = ParseEmbedded(_binary_device_setting_json_start,
                                       _binary_device_setting_json_end,
                                       "device_setting.json")) {
            return root;
        }

        extern const uint8_t _binary_eteacher_apps_jsons_ui_json_device_setting_json_start[] __attribute__((weak));
        extern const uint8_t _binary_eteacher_apps_jsons_ui_json_device_setting_json_end[] __attribute__((weak));
        return ParseEmbedded(_binary_eteacher_apps_jsons_ui_json_device_setting_json_start,
                             _binary_eteacher_apps_jsons_ui_json_device_setting_json_end,
                             "device_setting.json");
    }

    printf("[UiJson] Unknown UI JSON name: %s\n", name);
    return nullptr;
}

} // namespace app_ui
