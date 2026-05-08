#pragma once

#include <cstdint>
#include <string>

namespace word_practice {

enum class TimeSourceMode {
	WifiSystem,
	RtcChip,
};

const char *ToString(TimeSourceMode mode);
TimeSourceMode ParseTimeSourceMode(const std::string &value);
void SetPreferredTimeSourceMode(TimeSourceMode mode);
TimeSourceMode GetPreferredTimeSourceMode();

std::string CurrentCalendarDateString();
int64_t CurrentPersistentEpochSeconds();

}  // namespace word_practice
