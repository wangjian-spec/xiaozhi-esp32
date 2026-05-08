#pragma once

#include <cstdint>

namespace english_teacher {

void InitializeRtc();
bool ReadRtcEpochSeconds(int64_t* epoch_seconds);
bool WriteRtcEpochSeconds(int64_t epoch_seconds);

}  // namespace english_teacher