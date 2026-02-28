#include "debug.h"

#include <cstdio>
#include <functional>
#include "renderer.h"

namespace app_ui {
namespace debug {
// Only compile the real debug output when APP_UI_DEBUG is enabled.
#if APP_UI_DEBUG

void PrintSceneLoaded(const char* scene_id, uint16_t scene_index, Widget* root, bool has_public) {
    printf("[SceneRuntime] Loaded scene '%s' index=%u root=%p public=%d\n",
           scene_id ? scene_id : "(null)", static_cast<unsigned int>(scene_index), root, has_public ? 1 : 0);
}

void PrintSceneSwitch(Widget* prev_root, Widget* pending_root, uint32_t pending_focus_id) {
    printf("[UIEngine] Scene switch requested: prev_root=%p pending_root=%p pending_focus=%lu\n",
           prev_root, pending_root, static_cast<unsigned long>(pending_focus_id));
}

void PrintApplyPendingFocus(uint32_t pending_focus_id, bool result) {
    printf("[UIEngine] Applying pending focus id=%lu\n", static_cast<unsigned long>(pending_focus_id));
    printf("[UIEngine] Apply pending focus result=%d\n", result ? 1 : 0);
}

void DumpWidgetTree(Widget* root) {
    if (!root) return;
    std::function<void(Widget*, int)> dump;
    dump = [&](Widget* w, int depth) {
        if (!w) return;
        Rect r = w->RectInWindow();
        for (int i = 0; i < depth; ++i) printf("  ");
        printf("[UIEngine] W id=%lu ptr=%p vis=%d foc=%d focusable=%d rect=%d,%d,%d,%d\n",
               static_cast<unsigned long>(w->Id()), w,
               w->Visible() ? 1 : 0,
               w->Focused() ? 1 : 0,
               w->Focusable() ? 1 : 0,
               r.x, r.y, r.w, r.h);
        for (const auto& c : w->Children()) {
            dump(c.get(), depth + 1);
        }
    };
    printf("[UIEngine] Dumping widget tree:\n");
    dump(root, 0);
}

void PrintFocusSetById(uint32_t id, Widget* target) {
    printf("[FocusManager] SetCurrentById id=%lu target=%p\n", static_cast<unsigned long>(id), target);
}

void PrintFocusSwitch(uint32_t from_id, Widget* from_ptr, uint32_t to_id, Widget* to_ptr) {
    printf("[FocusManager] SetCurrent switching from id=%lu ptr=%p to id=%lu ptr=%p\n",
           static_cast<unsigned long>(from_id), from_ptr,
           static_cast<unsigned long>(to_id), to_ptr);
}

void PrintVisibleChange(uint32_t id, Widget* ptr, bool visible) {
    printf("[Widget] SetVisible id=%lu ptr=%p -> visible=%d\n", static_cast<unsigned long>(id), ptr, visible ? 1 : 0);
}

void PrintRenderItem(const RenderObject& obj) {
        printf("[Renderer] RenderItem widget=%p id=%lu rect=%d,%d,%d,%d depth=%lu z=%d order=%lu\n",
           obj.widget, obj.widget ? static_cast<unsigned long>(obj.widget->Id()) : 0UL,
           obj.rect.x, obj.rect.y, obj.rect.w, obj.rect.h,
            static_cast<unsigned long>(obj.depth), static_cast<int>(obj.z), static_cast<unsigned long>(obj.order));
}

void PrintRenderSkip(const RenderObject& obj, const char* reason) {
    printf("[Renderer] SkipItem widget=%p id=%lu reason=%s rect=%d,%d,%d,%d\n",
           obj.widget, obj.widget ? static_cast<unsigned long>(obj.widget->Id()) : 0UL,
           reason, obj.rect.x, obj.rect.y, obj.rect.w, obj.rect.h);
}

#endif // APP_UI_DEBUG

} // namespace debug
} // namespace app_ui
