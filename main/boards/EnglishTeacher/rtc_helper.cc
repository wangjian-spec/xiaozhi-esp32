#include "rtc_helper.h"

#include <esp_log.h>

namespace english_teacher {
namespace {

constexpr const char* kTag = "EnglishTeacherRtc";

bool g_rtc_initialized = false;
bool g_rtc_available = false;

void EnsureRtcInitialized() {
    if (g_rtc_initialized) {
        return;
    }

    g_rtc_initialized = true;
    g_rtc_available = false;
    ESP_LOGI(kTag, "RTC helper initialized; no RTC chip driver configured yet");
}

}  // namespace

void InitializeRtc() {
    EnsureRtcInitialized();
}

bool ReadRtcEpochSeconds(int64_t* epoch_seconds) {
    EnsureRtcInitialized();
    if (epoch_seconds != nullptr) {
        *epoch_seconds = 0;
    }
    return g_rtc_available;
}

bool WriteRtcEpochSeconds(int64_t epoch_seconds) {
    EnsureRtcInitialized();
    (void)epoch_seconds;
    return g_rtc_available;
}

}  // namespace english_teacher