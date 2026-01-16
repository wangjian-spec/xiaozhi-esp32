#pragma once

#include <memory>

#include "eteacher/app_manager/app_base.h"

// Calendar + Todo app
std::unique_ptr<AppBase> MakeCalendarScheduleApp();
