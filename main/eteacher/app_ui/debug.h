#pragma once

#include <cstdint>
#include "widget.h"

namespace app_ui {
struct RenderObject;

namespace debug {

void PrintSceneLoaded(const char* scene_id, uint16_t scene_index, Widget* root, bool has_public);
void PrintSceneSwitch(Widget* prev_root, Widget* pending_root, uint32_t pending_focus_id);
void PrintApplyPendingFocus(uint32_t pending_focus_id, bool result);
void PrintDebugVisibility(Widget* root, uint32_t tab_id, uint32_t saved_id);
void DumpWidgetTree(Widget* root);

void PrintRenderItem(const RenderObject& obj);
void PrintRenderSkip(const RenderObject& obj, const char* reason);

void PrintFocusSetById(uint32_t id, Widget* target);
void PrintFocusSwitch(uint32_t from_id, Widget* from_ptr, uint32_t to_id, Widget* to_ptr);

void PrintVisibleChange(uint32_t id, Widget* ptr, bool visible);

} // namespace debug
} // namespace app_ui
