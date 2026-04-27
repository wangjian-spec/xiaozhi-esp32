#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace eteacher::app_ui {

void SetWordResourceStage(int stage_index);
int GetWordResourceStage();

struct BinImage {
    const uint8_t* data = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    size_t data_size = 0;
};

struct OwnedBinImage {
    std::vector<uint8_t> storage;
    uint16_t width = 0;
    uint16_t height = 0;

    const uint8_t* data() const {
        return storage.empty() ? nullptr : storage.data();
    }

    size_t data_size() const {
        return storage.size();
    }

    void Clear() {
        storage.clear();
        width = 0;
        height = 0;
    }
};

const char* GetWordsImagePackagePath();
const char* GetWordsImageProxyDir();
std::string BuildWordsImageProxyPath(const std::string& name);

const char* GetWordsAudioDir();
const char* GetWordsAudioBundlePath();
std::string BuildWordsAudioPath(const std::string& audio_filename);

const char* GetExampleAudioDir();
const char* GetExampleAudioBundlePath();
std::string BuildExampleAudioPath(const std::string& audio_filename);

bool LoadBinImage(const std::string& name, BinImage* out);
bool LoadBinImageOwned(const std::string& name, OwnedBinImage* out);
bool LoadBinImageFallback(const std::vector<std::string>& names, BinImage* out);

int16_t GetFontHeight(std::string_view font_name);
int16_t GetFontAscent(std::string_view font_name);

} // namespace eteacher::app_ui
