#ifndef ETEACHER_ASSETS_H
#define ETEACHER_ASSETS_H

#include <map>
#include <string>
#include <functional>

#include <cJSON.h>
#include <esp_partition.h>
#include <model_path.h>


struct EteacherAsset {
    size_t size;
    size_t offset;
};

class EteacherAssets {
public:
    static EteacherAssets& GetInstance() {
        static EteacherAssets instance;
        return instance;
    }
    ~EteacherAssets();

    bool Download(std::string url, std::function<void(int progress, size_t speed)> progress_callback);
    bool Apply();
    bool GetAssetData(const std::string& name, void*& ptr, size_t& size);

    inline bool partition_valid() const { return partition_valid_; }
    inline bool checksum_valid() const { return checksum_valid_; }
    inline std::string default_assets_url() const { return default_assets_url_; }

private:
    EteacherAssets();
    EteacherAssets(const EteacherAssets&) = delete;
    EteacherAssets& operator=(const EteacherAssets&) = delete;

    bool InitializePartition();
    uint32_t CalculateChecksum(const char* data, uint32_t length);

    const esp_partition_t* partition_ = nullptr;
    esp_partition_mmap_handle_t mmap_handle_ = 0;
    const char* mmap_root_ = nullptr;
    bool partition_valid_ = false;
    bool checksum_valid_ = false;
    std::string default_assets_url_;
    srmodel_list_t* models_list_ = nullptr;
    std::map<std::string, EteacherAsset> assets_;
};

#endif
