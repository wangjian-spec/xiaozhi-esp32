#pragma once

#include <cstdint>
#include "widget.h"

namespace app_ui {
struct RenderObject;

namespace debug {

// Control debug printing. Define `APP_UI_DEBUG` to 0 to disable debug prints
#ifndef APP_UI_DEBUG
#define APP_UI_DEBUG 0
#endif

#if APP_UI_DEBUG
void PrintSceneLoaded(const char* scene_id, uint16_t scene_index, Widget* root, bool has_public);
void PrintSceneSwitch(Widget* prev_root, Widget* pending_root, uint32_t pending_focus_id);
void PrintApplyPendingFocus(uint32_t pending_focus_id, bool result);
void DumpWidgetTree(Widget* root);

void PrintRenderItem(const RenderObject& obj);
void PrintRenderSkip(const RenderObject& obj, const char* reason);

void PrintFocusSetById(uint32_t id, Widget* target);
void PrintFocusSwitch(uint32_t from_id, Widget* from_ptr, uint32_t to_id, Widget* to_ptr);

void PrintVisibleChange(uint32_t id, Widget* ptr, bool visible);
#else
inline void PrintSceneLoaded(const char*, uint16_t, Widget*, bool) {}
inline void PrintSceneSwitch(Widget*, Widget*, uint32_t) {}
inline void PrintApplyPendingFocus(uint32_t, bool) {}
inline void DumpWidgetTree(Widget*) {}

inline void PrintRenderItem(const RenderObject&) {}
inline void PrintRenderSkip(const RenderObject&, const char*) {}

inline void PrintFocusSetById(uint32_t, Widget*) {}
inline void PrintFocusSwitch(uint32_t, Widget*, uint32_t, Widget*) {}

inline void PrintVisibleChange(uint32_t, Widget*, bool) {}
#endif

} // namespace debug
} // namespace app_ui
