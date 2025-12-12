#include "storage/sd_resource.h"

#include <esp_log.h>
#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>

namespace {
constexpr char TAG[] = "SdResource";

// SDSPI pin mapping for EnglishTeacher board
constexpr int kPinMiso = 13;
constexpr int kPinMosi = 14;
constexpr int kPinSck  = 17;
constexpr int kPinCs   = 10;

// Desired SPI speed/mode for the SD card
constexpr uint32_t kSdSpiFreqHz = SD_SCK_MHZ(10);

// SdFat objects (shared across calls)
SdFat sd;

bool EnsureSdSpiBus() {
    static bool bus_ready = false;
    if (!bus_ready) {
        SPI.begin(kPinSck, kPinMiso, kPinMosi, kPinCs);
        pinMode(kPinCs, OUTPUT);
        digitalWrite(kPinCs, HIGH);
        ESP_LOGW(TAG, "Init SdFat shared SPI CS=%d MISO=%d MOSI=%d SCK=%d", kPinCs, kPinMiso, kPinMosi, kPinSck);
        bus_ready = true;
    }
    return bus_ready;
}

const char* kFontDir = "/fonts";
const char* kBitmapDir = "/images";
const char* kAudioDir = "/audio";
}

SdResource& SdResource::GetInstance() {
    static SdResource inst;
    return inst;
}

bool SdResource::Init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mounted_) return true;

    EnsureSdSpiBus();

    SdSpiConfig cfg(kPinCs, SHARED_SPI, kSdSpiFreqHz, &SPI);
    if (!sd.begin(cfg)) {
        ESP_LOGW(TAG, "SdFat begin failed err=0x%x data=0x%x", sd.sdErrorCode(), sd.sdErrorData());
        return false;
    }

    mounted_ = true;
    ESP_LOGI(TAG, "SD card ready via SdFat");
    return true;
}

bool SdResource::ReadFile(const std::string& relative_path, std::vector<uint8_t>& out) {
    if (!Init()) return false;
    std::string full_path = relative_path;
    if (!full_path.empty() && full_path[0] == '/') {
        if (full_path.rfind(mount_point_, 0) == 0) full_path = full_path.substr(mount_point_.size());
    }
    if (full_path.empty() || full_path[0] != '/') full_path = "/" + full_path;

    FsFile f = sd.open(full_path.c_str(), FILE_READ);
    if (!f) {
        ESP_LOGW(TAG, "Open failed: %s", full_path.c_str());
        return false;
    }
    const auto size = f.size();
    if (size <= 0) {
        ESP_LOGW(TAG, "Empty file: %s", full_path.c_str());
        f.close();
        return false;
    }
    out.resize(static_cast<size_t>(size));
    auto n = f.read(out.data(), size);
    f.close();
    if (n != size) {
        ESP_LOGW(TAG, "Read size mismatch: %s (%u/%u)", full_path.c_str(), static_cast<unsigned>(n), static_cast<unsigned>(size));
        return false;
    }
    return true;
}

bool SdResource::Exists(const std::string& relative_path) {
    if (!Init()) return false;
    std::string full_path = relative_path;
    if (!full_path.empty() && full_path[0] == '/') {
        if (full_path.rfind(mount_point_, 0) == 0) full_path = full_path.substr(mount_point_.size());
    }
    if (full_path.empty() || full_path[0] != '/') full_path = "/" + full_path;
    return sd.exists(full_path.c_str());
}

bool SdResource::LoadBinary(const std::string& relative_path, std::vector<uint8_t>& out, const char* label) {
    out.clear();
    const char* safe_label = label ? label : "sd";

    if (relative_path.empty()) {
        ESP_LOGW(TAG, "%s load failed: empty path", safe_label);
        return false;
    }

    if (!Exists(relative_path)) {
        ESP_LOGW(TAG, "%s load failed: file not found (%s)", safe_label, relative_path.c_str());
        return false;
    }

    if (!ReadFile(relative_path, out)) {
        ESP_LOGW(TAG, "%s load failed: read error (%s)", safe_label, relative_path.c_str());
        return false;
    }

    if (out.empty()) {
        ESP_LOGW(TAG, "%s load failed: zero size (%s)", safe_label, relative_path.c_str());
        return false;
    }

    ESP_LOGD(TAG, "%s load success: path=%s size=%zu", safe_label, relative_path.c_str(), out.size());
    return true;
}

bool SdResource::ReadFont(const std::string& filename, std::vector<uint8_t>& out) {
    return ReadFile(std::string(kFontDir).substr(1) + "/" + filename, out);
}

bool SdResource::ReadBitmap(const std::string& filename, std::vector<uint8_t>& out) {
    return ReadFile(std::string(kBitmapDir).substr(1) + "/" + filename, out);
}

bool SdResource::ReadAudio(const std::string& filename, std::vector<uint8_t>& out) {
    return ReadFile(std::string(kAudioDir).substr(1) + "/" + filename, out);
}
