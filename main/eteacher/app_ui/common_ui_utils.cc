#include "eteacher/app_ui/common_ui_utils.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <SD.h>
#include <esp_log.h>

#include "assets.h"
#include "eteacher/font_manager/font_manager.h"

namespace eteacher::app_ui {
namespace {

constexpr char kTag[] = "CommonUiUtils";
constexpr char kImagePackageMagic[] = {'I', 'P', 'K', '1'};
constexpr const char* kWordsImagePackagePath = "/sdcard/resource/image/words/stage_1.bin";
constexpr const char* kWordsImageProxyDir = "/resource/image/words/";
constexpr const char* kWordsAudioDir = "/resource/audio/words/";
constexpr const char* kWordsAudioBundlePath = "/resource/audio/words/stage_1.bin";

struct ImagePackageEntry {
    uint32_t offset = 0;
    uint32_t size = 0;
};

bool g_stage1_package_loaded = false;
bool g_stage1_package_available = false;
std::string g_stage1_package_path;
std::unordered_map<std::string, ImagePackageEntry> g_stage1_package_entries;

static inline uint16_t ReadLE16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

std::string BasenameFromPath(const std::string& path) {
    const size_t pos = path.find_last_of("\\/");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string NormalizePackageEntryName(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    while (!normalized.empty() && normalized.front() == '/') {
        normalized.erase(normalized.begin());
    }
    constexpr const char* kWordsImageProxyRelativeDir = "resource/image/words/";
    if (normalized.rfind(kWordsImageProxyRelativeDir, 0) == 0) {
        normalized.erase(0, std::strlen(kWordsImageProxyRelativeDir));
    }
    normalized = BasenameFromPath(normalized);
    if (normalized.empty()) {
        return {};
    }
    const size_t ext_pos = normalized.find_last_of('.');
    if (ext_pos == std::string::npos) {
        normalized += ".bin";
    } else {
        normalized.replace(ext_pos, std::string::npos, ".bin");
    }
    return normalized;
}

bool EnsureStage1ImagePackageLoaded() {
    if (g_stage1_package_loaded) {
        return g_stage1_package_available;
    }
    g_stage1_package_loaded = true;
    g_stage1_package_available = false;
    g_stage1_package_entries.clear();

    std::FILE* fp = std::fopen(kWordsImagePackagePath, "rb");
    g_stage1_package_path = kWordsImagePackagePath;
    if (fp == nullptr) {
        return false;
    }

    std::array<uint8_t, 20> header{};
    if (std::fread(header.data(), 1, header.size(), fp) != header.size() ||
        std::memcmp(header.data(), kImagePackageMagic, sizeof(kImagePackageMagic)) != 0) {
        std::fclose(fp);
        return false;
    }

    uint32_t entry_count = 0;
    std::memcpy(&entry_count, header.data() + 8, sizeof(entry_count));
    std::array<uint8_t, 14> entry_header{};
    for (uint32_t index = 0; index < entry_count; ++index) {
        if (std::fread(entry_header.data(), 1, entry_header.size(), fp) != entry_header.size()) {
            g_stage1_package_entries.clear();
            std::fclose(fp);
            return false;
        }
        uint16_t name_length = 0;
        ImagePackageEntry entry;
        std::memcpy(&name_length, entry_header.data(), sizeof(name_length));
        std::memcpy(&entry.offset, entry_header.data() + 2, sizeof(entry.offset));
        std::memcpy(&entry.size, entry_header.data() + 6, sizeof(entry.size));
        std::string entry_name(name_length, '\0');
        if (name_length > 0 && std::fread(entry_name.data(), 1, entry_name.size(), fp) != entry_name.size()) {
            g_stage1_package_entries.clear();
            std::fclose(fp);
            return false;
        }
        if (!entry_name.empty()) {
            g_stage1_package_entries.emplace(std::move(entry_name), entry);
        }
    }

    std::fclose(fp);
    g_stage1_package_available = !g_stage1_package_entries.empty();
    return g_stage1_package_available;
}

bool LoadStage1PackagedImage(const std::string& name, BinImage* out) {
    if (!out || !EnsureStage1ImagePackageLoaded()) {
        return false;
    }
    const std::string normalized_name = NormalizePackageEntryName(name);
    if (normalized_name.empty()) {
        return false;
    }
    const auto it = g_stage1_package_entries.find(normalized_name);
    if (it == g_stage1_package_entries.end() || it->second.size < 4) {
        return false;
    }

    std::FILE* fp = std::fopen(g_stage1_package_path.c_str(), "rb");
    if (fp == nullptr) {
        return false;
    }
    if (std::fseek(fp, static_cast<long>(it->second.offset), SEEK_SET) != 0) {
        std::fclose(fp);
        return false;
    }

    static std::vector<uint8_t> package_buffer;
    package_buffer.resize(static_cast<size_t>(it->second.size));
    const size_t read_size = std::fread(package_buffer.data(), 1, package_buffer.size(), fp);
    std::fclose(fp);
    if (read_size < 4) {
        return false;
    }

    const uint16_t w = ReadLE16(package_buffer.data());
    const uint16_t h = ReadLE16(package_buffer.data() + 2);
    const size_t stride = (w + 7u) / 8u;
    const size_t bytes = static_cast<size_t>(stride) * h;
    if (read_size < 4 + bytes || bytes == 0) {
        ESP_LOGW(kTag, "Packaged icon size mismatch: %s", normalized_name.c_str());
        return false;
    }

    out->data = package_buffer.data() + 4;
    out->width = w;
    out->height = h;
    out->data_size = bytes;
    return true;
}

} // namespace

const char* GetWordsImagePackagePath() {
    return kWordsImagePackagePath;
}

const char* GetWordsImageProxyDir() {
    return kWordsImageProxyDir;
}

std::string BuildWordsImageProxyPath(const std::string& name) {
    const std::string entry_name = NormalizePackageEntryName(BasenameFromPath(name));
    return entry_name.empty() ? std::string() : (std::string(kWordsImageProxyDir) + entry_name);
}

const char* GetWordsAudioDir() {
    return kWordsAudioDir;
}

const char* GetWordsAudioBundlePath() {
    return kWordsAudioBundlePath;
}

std::string BuildWordsAudioPath(const std::string& audio_filename) {
    std::string name = audio_filename;
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())) != 0) {
        name.erase(name.begin());
    }
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())) != 0) {
        name.pop_back();
    }
    if (name.empty()) {
        return {};
    }
    auto lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".mp3") == 0) {
        name.replace(name.size() - 4, 4, ".ogg");
    } else if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".opus") == 0) {
        name.replace(name.size() - 5, 5, ".ogg");
    }
    if (!name.empty() && name[0] == '/') {
        return name;
    }
    return std::string(kWordsAudioDir) + BasenameFromPath(name);
}

bool LoadBinImage(const std::string& name, BinImage* out) {
    if (!out) {
        return false;
    }
    void* ptr = nullptr;
    size_t size = 0;
    if (!Assets::GetInstance().GetAssetData(name, ptr, size) || !ptr || size < 4) {
        if (LoadStage1PackagedImage(name, out)) {
            return true;
        }

        const std::string path = name;
        if (!path.empty() && path.rfind("/sdcard/resource/image/", 0) == 0) {
            FILE* fp = std::fopen(path.c_str(), "rb");
            if (!fp) {
                File sd_file = SD.open(path.c_str(), FILE_READ);
                if (!sd_file) {
                    ESP_LOGW(kTag, "Icon not found or too small: %s", name.c_str());
                    return false;
                }
                const size_t file_size = static_cast<size_t>(sd_file.size());
                if (file_size < 4) {
                    sd_file.close();
                    ESP_LOGW(kTag, "Icon not found or too small: %s", name.c_str());
                    return false;
                }

                static std::vector<uint8_t> file_buffer;
                file_buffer.resize(file_size);
                const size_t read_size = sd_file.readBytes(reinterpret_cast<char*>(file_buffer.data()), static_cast<int>(file_size));
                sd_file.close();
                if (read_size < 4) {
                    ESP_LOGW(kTag, "Icon not found or too small: %s", name.c_str());
                    return false;
                }

                const uint16_t w = ReadLE16(file_buffer.data());
                const uint16_t h = ReadLE16(file_buffer.data() + 2);
                const size_t stride = (w + 7u) / 8u;
                const size_t bytes = static_cast<size_t>(stride) * h;
                if (read_size < 4 + bytes || bytes == 0) {
                    ESP_LOGW(kTag, "Icon file size mismatch: %s", path.c_str());
                    return false;
                }

                out->data = file_buffer.data() + 4;
                out->width = w;
                out->height = h;
                out->data_size = bytes;
                return true;
            }
            if (std::fseek(fp, 0, SEEK_END) != 0) {
                std::fclose(fp);
                ESP_LOGW(kTag, "Icon not found or too small: %s", name.c_str());
                return false;
            }
            const long file_size_long = std::ftell(fp);
            if (file_size_long < 4) {
                std::fclose(fp);
                ESP_LOGW(kTag, "Icon not found or too small: %s", name.c_str());
                return false;
            }
            const size_t file_size = static_cast<size_t>(file_size_long);
            if (std::fseek(fp, 0, SEEK_SET) != 0) {
                std::fclose(fp);
                ESP_LOGW(kTag, "Icon not found or too small: %s", name.c_str());
                return false;
            }

            static std::vector<uint8_t> file_buffer;
            file_buffer.resize(file_size);
            const size_t read_size = std::fread(file_buffer.data(), 1, file_size, fp);
            std::fclose(fp);
            if (read_size < 4) {
                ESP_LOGW(kTag, "Icon not found or too small: %s", name.c_str());
                return false;
            }

            const uint16_t w = ReadLE16(file_buffer.data());
            const uint16_t h = ReadLE16(file_buffer.data() + 2);
            const size_t stride = (w + 7u) / 8u;
            const size_t bytes = static_cast<size_t>(stride) * h;
            if (read_size < 4 + bytes || bytes == 0) {
                ESP_LOGW(kTag, "Icon file size mismatch: %s", path.c_str());
                return false;
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
