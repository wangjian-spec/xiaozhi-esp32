#include "renderer.h"

#include <algorithm>

#include "painter.h"
#include "widget.h"

namespace app_ui {

void RenderList::Clear() {
    items_.clear();
}

void RenderList::Build(Widget* root) {
    items_.clear();
    uint32_t order = 0;
    Traverse(root, 0, order);
    std::stable_sort(items_.begin(), items_.end(), [](const RenderObject& a, const RenderObject& b) {
        if (a.depth != b.depth) {
            return a.depth < b.depth;
        }
        if (a.z != b.z) {
            return a.z < b.z;
        }
        return a.order < b.order;
    });
}

const std::vector<RenderObject>& RenderList::Items() const {
    return items_;
}

void RenderList::Traverse(Widget* node, uint16_t depth, uint32_t& order) {
    if (!node || !node->Visible()) {
        return;
    }
    RenderObject obj;
    obj.rect = node->RectInWindow();
    obj.widget = node;
    obj.z = node->ZOrder();
    obj.depth = depth;
    obj.order = order++;
    items_.push_back(obj);

    for (const auto& child : node->Children()) {
        Traverse(child.get(), static_cast<uint16_t>(depth + 1), order);
    }
}

const RenderCapabilities& Renderer::Capabilities() const {
    return caps_;
}

void Renderer::Render(RenderList& list, DirtyTracker& dirty, Painter& painter) {
    if (!dirty.HasDirty()) {
        return;
    }

    if (dirty.RequireFullRefresh()) {
        FullRefresh();
    } else {
        PartialRefresh(dirty.Merge());
    }

    for (const auto& item : list.Items()) {
        if (!item.widget) {
            continue;
        }
        painter.PushClip(item.rect);
        painter.SetTransform({item.rect.x, item.rect.y});
        item.widget->Draw(painter);
        painter.SetTransform({0, 0});
        painter.PopClip();
    }

    dirty.Clear();
}

void Renderer::PartialRefresh(const Rect&) {
}

void Renderer::FullRefresh() {
}

} // namespace app_ui
