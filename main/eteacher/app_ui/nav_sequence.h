#pragma once

#include <vector>

#include "eteacher/app_manager/app_base.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "eteacher/app_ui/seq_button.h"
#ifdef __cplusplus
}
#endif

namespace eteacher::app_ui::nav {

std::vector<AppButton> ToAppButtonSequence(const seq_event_t* evt);
void ApplyGridMoveSequence(int& row, int& col, int rows, int cols, const std::vector<AppButton>& seq);

} // namespace eteacher::app_ui::nav
