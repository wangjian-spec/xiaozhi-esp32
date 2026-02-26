#include "eteacher/app_ui/common_ui_utils.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <esp_log.h>

#include "assets.h"
#include "eteacher/font_manager/font_manager.h"

namespace eteacher::app_ui {
namespace {

constexpr char kTag[] = "CommonUiUtils";

static inline uint16_t ReadLE16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

} // namespace

bool LoadBinImage(const std::string& name, BinImage* out) {
    if (!out) {
        return false;
    }
    void* ptr = nullptr;
    size_t size = 0;
    if (!Assets::GetInstance().GetAssetData(name, ptr, size) || !ptr || size < 4) {
        std::vector<std::string> candidates;
        candidates.push_back(name);
        if (!name.empty() && name[0] != '/') {
            candidates.push_back("/sdcard/resource/image/" + name);
        }

        for (const auto& path : candidates) {
            FILE* fp = std::fopen(path.c_str(), "rb");
            if (!fp) {
                continue;
            }
            if (std::fseek(fp, 0, SEEK_END) != 0) {
                std::fclose(fp);
                continue;
            }
            const long file_size_long = std::ftell(fp);
            if (file_size_long < 4) {
                std::fclose(fp);
                continue;
            }
            const size_t file_size = static_cast<size_t>(file_size_long);
            if (std::fseek(fp, 0, SEEK_SET) != 0) {
                std::fclose(fp);
                continue;
            }

            static std::vector<uint8_t> file_buffer;
            file_buffer.resize(file_size);
            const size_t read_size = std::fread(file_buffer.data(), 1, file_size, fp);
            std::fclose(fp);
            if (read_size < 4) {
                continue;
            }

            const uint16_t w = ReadLE16(file_buffer.data());
            const uint16_t h = ReadLE16(file_buffer.data() + 2);
            const size_t stride = (w + 7u) / 8u;
            const size_t bytes = static_cast<size_t>(stride) * h;
            if (read_size < 4 + bytes || bytes == 0) {
                ESP_LOGW(kTag, "Icon file size mismatch: %s", path.c_str());
                continue;
            }

            out->data = file_buffer.data() + 4;
            out->width = w;
            out->height = h;
            out->data_size = bytes;
            return true;
        }

        ESP_LOGW(kTag, "Icon not found or too small: %s", name.c_str());
        return false;
    }

    const auto* data = static_cast<const uint8_t*>(ptr);
    const uint16_t w = ReadLE16(data);
    const uint16_t h = ReadLE16(data + 2);
    const size_t stride = (w + 7u) / 8u;
    const size_t bytes = static_cast<size_t>(stride) * h;

    if (size < 4 + bytes) {
        ESP_LOGW(kTag, "Icon size mismatch: %s", name.c_str());
        static std::vector<uint8_t> scratch;
        if (scratch.size() < bytes) {
            scratch.resize(bytes);
        }
        std::fill(scratch.begin(), scratch.begin() + bytes, 0x00);
        const size_t available = size > 4 ? (size - 4) : 0;
        if (available > 0) {
            std::memcpy(scratch.data(), data + 4, std::min(available, bytes));
        }
        out->data = scratch.data();
        out->width = w;
        out->height = h;
        out->data_size = bytes;
        return true;
    }

    out->data = data + 4;
    out->width = w;
    out->height = h;
    out->data_size = bytes;
    return true;
}

bool LoadBinImageFallback(const std::vector<std::string>& names, BinImage* out) {
    for (const auto& name : names) {
        if (LoadBinImage(name, out)) {
            return true;
        }
    }
    return false;
}

int16_t GetFontHeight(std::string_view font_name) {
    const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
    if (!font || !font->Ready()) {
        return 12;
    }
    const auto& header = font->Header();
    return static_cast<int16_t>(header.ascent + header.descent);
}

int16_t GetFontAscent(std::string_view font_name) {
    const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
    if (!font || !font->Ready()) {
        return 9;
    }
    return static_cast<int16_t>(font->Header().ascent);
}

} // namespace eteacher::app_ui
