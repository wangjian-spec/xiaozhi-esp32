#pragma once

#include <memory>

#include "eteacher/app_manager/app_base.h"

std::unique_ptr<AppBase> MakeWordSnakeApp();
