#pragma once

#include <memory>

#include "app_manager/app_base.h"

// Calendar + Todo app
std::unique_ptr<AppBase> MakeCalendarScheduleApp();
