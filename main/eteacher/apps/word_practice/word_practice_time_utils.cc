#include "eteacher/apps/word_practice/word_practice_time_utils.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include <esp_timer.h>

#include "boards/EnglishTeacher/rtc_helper.h"

namespace word_practice {
namespace {

constexpr int64_t kMinimumPlausibleEpochSeconds = 1704067200;  // 2024-01-01 00:00:00 UTC
constexpr const char *kWifiSystemTimeSourceName = "wifi_system";
constexpr const char *kRtcChipTimeSourceName = "rtc_chip";

TimeSourceMode g_preferred_time_source_mode = TimeSourceMode::WifiSystem;

bool IsPlausibleEpochSeconds(int64_t value) {
	return value >= kMinimumPlausibleEpochSeconds;
}

int64_t UptimeFallbackEpochSeconds() {
	const int64_t uptime_sec = static_cast<int64_t>(esp_timer_get_time() / 1000000ULL);
	return kMinimumPlausibleEpochSeconds + std::max<int64_t>(0, uptime_sec);
}

bool TryReadSystemEpochSeconds(int64_t *epoch_seconds) {
	if (epoch_seconds == nullptr) {
		return false;
	}
	const std::time_t now = std::time(nullptr);
	if (!IsPlausibleEpochSeconds(static_cast<int64_t>(now))) {
		return false;
	}
	*epoch_seconds = static_cast<int64_t>(now);
	return true;
}

bool TryReadRtcChipEpochSeconds(int64_t *epoch_seconds) {
	if (epoch_seconds == nullptr) {
		return false;
	}
	int64_t rtc_epoch_seconds = 0;
	if (!english_teacher::ReadRtcEpochSeconds(&rtc_epoch_seconds)) {
		return false;
	}
	if (!IsPlausibleEpochSeconds(rtc_epoch_seconds)) {
		return false;
	}
	*epoch_seconds = rtc_epoch_seconds;
	return true;
}

int64_t ResolveCurrentEpochSeconds() {
	int64_t resolved_epoch_seconds = 0;
	if (g_preferred_time_source_mode == TimeSourceMode::RtcChip && TryReadRtcChipEpochSeconds(&resolved_epoch_seconds)) {
		return resolved_epoch_seconds;
	}
	if (TryReadSystemEpochSeconds(&resolved_epoch_seconds)) {
		return resolved_epoch_seconds;
	}
	if (TryReadRtcChipEpochSeconds(&resolved_epoch_seconds)) {
		return resolved_epoch_seconds;
	}
	return UptimeFallbackEpochSeconds();
}

}  // namespace

const char *ToString(TimeSourceMode mode) {
	switch (mode) {
		case TimeSourceMode::RtcChip:
			return kRtcChipTimeSourceName;
		case TimeSourceMode::WifiSystem:
		default:
			return kWifiSystemTimeSourceName;
	}
}

TimeSourceMode ParseTimeSourceMode(const std::string &value) {
	if (value == kRtcChipTimeSourceName) {
		return TimeSourceMode::RtcChip;
	}
	return TimeSourceMode::WifiSystem;
}

void SetPreferredTimeSourceMode(TimeSourceMode mode) {
	g_preferred_time_source_mode = mode;
}

TimeSourceMode GetPreferredTimeSourceMode() {
	return g_preferred_time_source_mode;
}

std::string CurrentCalendarDateString() {
	const std::time_t now = static_cast<std::time_t>(ResolveCurrentEpochSeconds());
	std::tm calendar_time = {};
	if (now >= 0) {
		localtime_r(&now, &calendar_time);
	}

	char buf[32] = {0};
	std::snprintf(buf,
		     sizeof(buf),
		     "%04d-%02d-%02d",
		     calendar_time.tm_year + 1900,
		     calendar_time.tm_mon + 1,
		     calendar_time.tm_mday);
	return buf;
}

int64_t CurrentPersistentEpochSeconds() {
	return ResolveCurrentEpochSeconds();
}

}  // namespace word_practice
