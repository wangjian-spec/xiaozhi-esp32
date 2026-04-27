#include "eteacher/apps/word_practice/word_practice_time_utils.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include <esp_timer.h>

namespace word_practice {
namespace {

constexpr int64_t kMinimumPlausibleEpochSeconds = 1704067200;  // 2024-01-01 00:00:00 UTC

}  // namespace

std::string CurrentCalendarDateString() {
	const std::time_t now = std::time(nullptr);
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
	const std::time_t now = std::time(nullptr);
	if (now >= static_cast<std::time_t>(kMinimumPlausibleEpochSeconds)) {
		return static_cast<int64_t>(now);
	}

	const int64_t uptime_sec = static_cast<int64_t>(esp_timer_get_time() / 1000000ULL);
	return kMinimumPlausibleEpochSeconds + std::max<int64_t>(0, uptime_sec);
}

}  // namespace word_practice
