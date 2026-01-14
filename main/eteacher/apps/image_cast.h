#pragma once

#include <memory>

#include "eteacher/app_manager/app_base.h"

// ImageCastApp: Upload a phone image over WiFi and render it on the EPD.
// Implementation is mostly self-contained within this app.
std::unique_ptr<AppBase> MakeImageCastApp();
