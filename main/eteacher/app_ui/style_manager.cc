#include "style_manager.h"

namespace app_ui {

void StyleManager::MarkDirty(bool layout) {
    if (layout) {
        layout_dirty_ = true;
    }
    render_dirty_ = true;
}

bool StyleManager::ConsumeLayoutDirty() {
    bool v = layout_dirty_;
    layout_dirty_ = false;
    return v;
}

bool StyleManager::ConsumeRenderDirty() {
    bool v = render_dirty_;
    render_dirty_ = false;
    return v;
}

} // namespace app_ui
