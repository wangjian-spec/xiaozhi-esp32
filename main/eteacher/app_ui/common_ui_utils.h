#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace eteacher::app_ui {

struct BinImage {
    const uint8_t* data = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    size_t data_size = 0;
};

bool LoadBinImage(const std::string& name, BinImage* out);
bool LoadBinImageFallback(const std::vector<std::string>& names, BinImage* out);

int16_t GetFontHeight(std::string_view font_name);
int16_t GetFontAscent(std::string_view font_name);

} // namespace eteacher::app_ui
