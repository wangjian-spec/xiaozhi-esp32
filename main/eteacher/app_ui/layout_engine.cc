#include "layout_engine.h"
#include "widget.h"

namespace app_ui {

void LayoutEngine::LayoutTree(Widget* root, const Rect& area) {
    if (!root) {
        return;
    }
    LayoutRecursive(root, area);
}

void LayoutEngine::LayoutRecursive(Widget* node, const Rect& area) {
    if (!node) {
        return;
    }
    node->Measure({area.w, area.h});
    node->Layout(area);

    const Rect parent_rect = node->RectInParent();
    for (const auto& child : node->Children()) {
        Rect child_rect = child->RectInParent();
        if (child_rect.IsEmpty()) {
            child_rect = {0, 0, parent_rect.w, parent_rect.h};
        }
        LayoutRecursive(child.get(), child_rect);
    }
}

} // namespace app_ui
